// Package goldentest replays the CodeGeneratorRequests recorded by scripts/gen-testdata.sh
// through a generator and compares the output with the checksums of the files produced by the
// upstream plugin.
//
// Layout (d = testdata/checksums/<owner>/<plugin>, see CONTRIBUTING.md):
//
//	d/<version>.buf.gen.yaml        option groups (plugin entries) and tests (inputs)
//	d/<version>.tar.xz              <test>.request.binpb   request sent by buf, parameter cleared
//	                                <group>/parameter.txt  parameter sent by buf for the group
//	                                <group>/<test>.sha256  sha256sum of the upstream output
//	d/gen/<version>/<group>/<test>/ upstream output (optional, not committed)
//
// When the output does not match the checksums and the upstream output exists, the test shows a
// content diff.
//
// Set UNI_GOLDEN_OUT=<dir> to also write the actual output to <dir>/<owner>/<plugin>/<version>/<group>/<test>/,
// which is handy for `diff -r` against d/gen/<version>.
package goldentest

import (
	"archive/tar"
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
	"testing"

	"github.com/ulikunitz/xz"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/pluginpb"

	"github.com/exa-pub/protoc-gen-uni/pkg/uni"
)

const (
	requestSuffix = ".request.binpb"
	sumSuffix     = ".sha256"
	parameterFile = "parameter.txt"
)

// Data is the committed test data of one generator version.
type Data struct {
	Name, Version string
	Template      string // path of <version>.buf.gen.yaml
	Archive       string // path of <version>.tar.xz
	GenDir        string // path of gen/<version> (may not exist)
	Tests         []string
	Groups        []string
	files         map[string][]byte // archive contents
}

// Run runs all golden tests of gen as subtests named <group>/<test>.
func Run(t *testing.T, gen uni.Generator) {
	t.Helper()
	data, err := Load(Root(t), gen.Name, gen.Version)
	if err != nil {
		t.Fatal(err)
	}
	for _, group := range data.Groups {
		for _, test := range data.Tests {
			t.Run(group+"/"+test, func(t *testing.T) {
				data.run(t, gen, group, test)
			})
		}
	}
}

// Dir returns testdata/checksums/<owner>/<plugin>.
func Dir(root, name string) string {
	return filepath.Join(root, "testdata", "checksums", filepath.FromSlash(name))
}

// Load reads <version>.tar.xz and checks that it is complete and matches <version>.buf.gen.yaml.
func Load(root, name, version string) (*Data, error) {
	dir := Dir(root, name)
	d := &Data{
		Name:     name,
		Version:  version,
		Template: filepath.Join(dir, version+".buf.gen.yaml"),
		Archive:  filepath.Join(dir, version+".tar.xz"),
		GenDir:   filepath.Join(dir, "gen", version),
	}
	regen := fmt.Sprintf("; run scripts/gen-testdata.sh %s %s", name, version)
	template, err := os.ReadFile(d.Template)
	if err != nil {
		return nil, err
	}
	compressed, err := os.ReadFile(d.Archive)
	if err != nil {
		return nil, fmt.Errorf("%w%s", err, regen)
	}
	if d.files, err = readTarXz(compressed); err != nil {
		return nil, fmt.Errorf("%s: %w", d.Archive, err)
	}

	for name := range d.files {
		switch dir, base := path.Split(name); {
		case dir == "" && strings.HasSuffix(base, requestSuffix):
			d.Tests = append(d.Tests, strings.TrimSuffix(base, requestSuffix))
		case base == parameterFile && strings.Count(name, "/") == 1:
			d.Groups = append(d.Groups, strings.TrimSuffix(dir, "/"))
		}
	}
	slices.Sort(d.Tests)
	slices.Sort(d.Groups)

	if want := TemplateTests(template); !slices.Equal(d.Tests, want) {
		return nil, fmt.Errorf("%s: tests %v, but %s has inputs %v%s", d.Archive, d.Tests, d.Template, want, regen)
	}
	if want := TemplateGroups(template); !slices.Equal(d.Groups, want) {
		return nil, fmt.Errorf("%s: groups %v, but %s has outs %v%s", d.Archive, d.Groups, d.Template, want, regen)
	}
	for _, g := range d.Groups {
		for _, test := range d.Tests {
			if _, ok := d.files[g+"/"+test+sumSuffix]; !ok {
				return nil, fmt.Errorf("%s: %s/%s%s missing%s", d.Archive, g, test, sumSuffix, regen)
			}
		}
	}
	return d, nil
}

var (
	inputRe = regexp.MustCompile(`(?m)^\s*-\s*directory:\s*testdata/proto/([A-Za-z0-9._-]+)\s*$`)
	outRe   = regexp.MustCompile(`(?m)^\s*-?\s*out:\s*([^\s#]+)`)
)

// TemplateTests returns the sorted tests listed in a buf.gen.yaml as
// `inputs: [- directory: testdata/proto/<test>]` (the format enforced by scripts/gen-testdata.sh).
func TemplateTests(template []byte) []string { return submatches(inputRe, template) }

// TemplateGroups returns the sorted option groups (`out:` of plugin entries) of a buf.gen.yaml.
func TemplateGroups(template []byte) []string { return submatches(outRe, template) }

func submatches(re *regexp.Regexp, b []byte) []string {
	var out []string
	for _, m := range re.FindAllSubmatch(b, -1) {
		out = append(out, string(m[1]))
	}
	slices.Sort(out)
	return out
}

func readTarXz(compressed []byte) (map[string][]byte, error) {
	xr, err := xz.NewReader(bytes.NewReader(compressed))
	if err != nil {
		return nil, err
	}
	files := map[string][]byte{}
	tr := tar.NewReader(xr)
	for {
		h, err := tr.Next()
		if errors.Is(err, io.EOF) {
			return files, nil
		}
		if err != nil {
			return nil, err
		}
		if h.Typeflag != tar.TypeReg {
			continue
		}
		b, err := io.ReadAll(tr)
		if err != nil {
			return nil, err
		}
		files[path.Clean(h.Name)] = b
	}
}

func (d *Data) run(t *testing.T, gen uni.Generator, group, test string) {
	req := &pluginpb.CodeGeneratorRequest{}
	if err := proto.Unmarshal(d.files[test+requestSuffix], req); err != nil {
		t.Fatal(err)
	}
	param := uni.ParamName + "=" + gen.Name + "," + uni.ParamVersion + "=" + gen.Version
	if recorded := d.files[group+"/"+parameterFile]; len(recorded) > 0 {
		param += "," + string(recorded)
	}
	req.Parameter = proto.String(param)

	resp := uni.Run(req, []uni.Generator{gen})
	if resp.Error != nil {
		t.Fatalf("generator error: %s", resp.GetError())
	}
	got := map[string]string{}
	for _, f := range resp.File {
		switch {
		case f.GetName() == "":
			t.Fatalf("response file without name (continuation files are not supported)")
		case f.GetInsertionPoint() != "":
			t.Fatalf("%s: insertion points are not supported", f.GetName())
		}
		if _, dup := got[f.GetName()]; dup {
			t.Fatalf("%s: generated twice", f.GetName())
		}
		got[f.GetName()] = f.GetContent()
	}
	if out := os.Getenv("UNI_GOLDEN_OUT"); out != "" {
		writeFiles(t, filepath.Join(out, filepath.FromSlash(gen.Name), gen.Version, group, test), got)
	}

	want, err := parseSums(d.files[group+"/"+test+sumSuffix])
	if err != nil {
		t.Fatalf("%s/%s%s: %v", group, test, sumSuffix, err)
	}
	var differ []string
	for _, name := range sortedKeys(want) {
		g, ok := got[name]
		switch {
		case !ok:
			t.Errorf("%s: not generated", name)
		case Sum(g) != want[name]:
			differ = append(differ, name)
		}
	}
	for _, name := range sortedKeys(got) {
		if _, ok := want[name]; !ok {
			t.Errorf("%s: generated, but not expected", name)
		}
	}
	if len(differ) == 0 {
		return
	}

	upstreamDir := filepath.Join(d.GenDir, group, test)
	upstream := readFiles(t, upstreamDir)
	if len(upstream) == 0 {
		for _, name := range differ {
			t.Errorf("%s: checksum differs", name)
		}
		t.Logf("run scripts/gen-testdata.sh %s %s to get the upstream output into %s and see diffs", d.Name, d.Version, upstreamDir)
		return
	}
	for _, name := range differ {
		if u, ok := upstream[name]; ok && Sum(u) == want[name] {
			t.Errorf("%s: content differs\n%s", name, Diff(u, got[name]))
		} else {
			t.Errorf("%s: checksum differs; %s is stale, rerun scripts/gen-testdata.sh %s %s to see a diff",
				name, upstreamDir, d.Name, d.Version)
		}
	}
}

// Root returns the repository root (the directory with go.mod).
func Root(t *testing.T) string {
	t.Helper()
	dir, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	for {
		if _, err := os.Stat(filepath.Join(dir, "go.mod")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			t.Fatal("go.mod not found")
		}
		dir = parent
	}
}

// Sum returns the hex sha256 of content.
func Sum(content string) string {
	h := sha256.Sum256([]byte(content))
	return hex.EncodeToString(h[:])
}

// parseSums parses sha256sum output ("<hex>  <path>" per line) into a path → hash map.
func parseSums(b []byte) (map[string]string, error) {
	sums := map[string]string{}
	for i, line := range strings.Split(strings.TrimRight(string(b), "\n"), "\n") {
		if line == "" {
			continue
		}
		sum, name, ok := strings.Cut(line, "  ")
		if !ok || len(sum) != sha256.Size*2 || name == "" {
			return nil, fmt.Errorf("line %d: want \"<sha256>  <path>\"", i+1)
		}
		sums[name] = sum
	}
	return sums, nil
}

func readFiles(t *testing.T, dir string) map[string]string {
	files := map[string]string{}
	err := filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		b, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		rel, err := filepath.Rel(dir, path)
		if err != nil {
			return err
		}
		files[filepath.ToSlash(rel)] = string(b)
		return nil
	})
	if err != nil && !errors.Is(err, fs.ErrNotExist) {
		t.Fatal(err)
	}
	return files
}

func writeFiles(t *testing.T, dir string, files map[string]string) {
	if err := os.RemoveAll(dir); err != nil {
		t.Fatal(err)
	}
	for name, content := range files {
		path := filepath.Join(dir, filepath.FromSlash(name))
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
	}
}

func sortedKeys(m map[string]string) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	slices.Sort(keys)
	return keys
}

// Diff describes the first difference between want and got with a few lines of context.
func Diff(want, got string) string {
	const context = 3
	wl, gl := strings.Split(want, "\n"), strings.Split(got, "\n")
	i := 0
	for i < len(wl) && i < len(gl) && wl[i] == gl[i] {
		i++
	}
	var b strings.Builder
	fmt.Fprintf(&b, "first difference at line %d (want %d lines, got %d lines):\n", i+1, len(wl), len(gl))
	for j := max(0, i-context); j < i; j++ {
		fmt.Fprintf(&b, "  %s\n", wl[j])
	}
	for j := i; j < min(len(wl), i+context); j++ {
		fmt.Fprintf(&b, "- %q\n", wl[j])
	}
	for j := i; j < min(len(gl), i+context); j++ {
		fmt.Fprintf(&b, "+ %q\n", gl[j])
	}
	return b.String()
}
