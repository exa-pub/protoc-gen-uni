package generators

import (
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
	"testing"

	"github.com/exa-pub/protoc-gen-uni/internal/goldentest"
)

var (
	nameRe    = regexp.MustCompile(`^[a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9-]*$`)
	versionRe = regexp.MustCompile(`^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$`)
)

// TestRegistry checks that the registry, generators/ and testdata/checksums/ are consistent.
// The golden tests themselves live next to each generator (golden_test.go).
func TestRegistry(t *testing.T) {
	root := goldentest.Root(t)
	registered := map[string]bool{}
	var keys []string
	for _, g := range All {
		key := g.Name + "/" + g.Version
		keys = append(keys, key)
		if registered[key] {
			t.Errorf("%s: registered twice", key)
		}
		registered[key] = true
		if !nameRe.MatchString(g.Name) {
			t.Errorf("%s: name must be <owner>/<plugin> as on buf.build", key)
		}
		if !versionRe.MatchString(g.Version) {
			t.Errorf("%s: version must be vX.Y.Z", key)
		}
		if g.Generate == nil {
			t.Errorf("%s: Generate is nil", key)
		}
		src := filepath.Join(root, "generators", filepath.FromSlash(key))
		mustExist(t, filepath.Join(src, "golden_test.go"))
		readme := filepath.Join(root, "generators", filepath.FromSlash(g.Name), "README.md")
		if b, err := os.ReadFile(readme); err != nil {
			t.Errorf("%s: %v", key, err)
		} else if !strings.Contains(string(b), g.Version) {
			t.Errorf("%s: README.md does not mention version %s", readme, g.Version)
		}
		data, err := goldentest.Load(root, g.Name, g.Version)
		if err != nil {
			t.Error(err)
			continue
		}
		for _, test := range data.Tests {
			mustExist(t, filepath.Join(root, "testdata", "proto", test))
		}
	}
	if !slices.IsSorted(keys) {
		t.Errorf("All must be sorted by name, then version: %v", keys)
	}

	// generators/<owner>/<plugin>/<version>/ and testdata/checksums/<owner>/<plugin>/<version>.<ext>
	checks := []struct{ base, pattern, suffix string }{
		{"generators", "v*", ""},
		{filepath.Join("testdata", "checksums"), "v*.buf.gen.yaml", ".buf.gen.yaml"},
		{filepath.Join("testdata", "checksums"), "v*.tar.xz", ".tar.xz"},
	}
	for _, c := range checks {
		paths, err := filepath.Glob(filepath.Join(root, c.base, "*", "*", c.pattern))
		if err != nil {
			t.Fatal(err)
		}
		for _, p := range paths {
			rel, err := filepath.Rel(filepath.Join(root, c.base), p)
			if err != nil {
				t.Fatal(err)
			}
			if !registered[strings.TrimSuffix(filepath.ToSlash(rel), c.suffix)] {
				t.Errorf("%s: not registered in generators/registry.go", filepath.Join(c.base, rel))
			}
		}
	}
}

func mustExist(t *testing.T, path string) {
	t.Helper()
	if _, err := os.Stat(path); err != nil {
		t.Error(err)
	}
}
