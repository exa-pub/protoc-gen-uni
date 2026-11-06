// Copyright 2021-2026 Buf Technologies, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package protoplugin

import (
	"regexp"
	"strings"
)

// Ported from packages/protoplugin/src/import-symbol.ts and import-path.ts.

// ImportSymbol represents an ECMAScript import.
type ImportSymbol struct {
	// Name is the name to import.
	Name string
	// From is the import path: a package, for example `@foo/bar/baz.js`, or a file relative to the
	// project root, for example `./bar/baz.js`.
	From string
	// TypeOnly reports whether this is a type-only import.
	TypeOnly bool
}

// NewImportSymbol creates a new import symbol.
func NewImportSymbol(name, from string, typeOnly bool) *ImportSymbol {
	return &ImportSymbol{Name: name, From: from, TypeOnly: typeOnly}
}

// ToTypeOnly returns a type-only copy of this import.
func (s *ImportSymbol) ToTypeOnly() *ImportSymbol {
	return &ImportSymbol{Name: s.Name, From: s.From, TypeOnly: true}
}

// ID is the unique ID based on name and from, disregarding typeOnly.
func (s *ImportSymbol) ID() string {
	return `import("` + s.From + `").` + s.Name
}

type compiledPattern struct {
	pattern *regexp.Regexp
	target  string
}

// RewriteImportPath applies import rewrites to the given import path, and changes the .js
// extension of relative import paths to the given import extension.
func RewriteImportPath(importPath string, rewriteImports []compiledPattern, importExtension ImportExtension) string {
	for _, ri := range rewriteImports {
		if ri.pattern.MatchString(importPath) {
			if relativePathRE.MatchString(importPath) {
				importPath = strings.TrimSuffix(ri.target, "/") + relativePathRE.ReplaceAllString(importPath, "/")
			} else {
				importPath = ri.target
			}
			break
		}
	}
	if relativePathRE.MatchString(importPath) && strings.HasSuffix(importPath, ".js") {
		switch importExtension {
		case "none":
			return importPath[:len(importPath)-3]
		case "ts", "js":
			return importPath[:len(importPath)-2] + string(importExtension)
		}
	}
	return importPath
}

func compileRewriteImports(rewriteImports []PatternTarget) []compiledPattern {
	out := make([]compiledPattern, 0, len(rewriteImports))
	for _, ri := range rewriteImports {
		out = append(out, compiledPattern{starToRegExp(ri.Pattern), ri.Target})
	}
	return out
}

func starToRegExp(star string) *regexp.Regexp {
	var r strings.Builder
	r.WriteString("^")
	for i := 0; i < len(star); i++ {
		switch c := star[i]; c {
		case '*':
			if i+2 < len(star) && star[i+1] == '*' && star[i+2] == '/' {
				i += 2
				r.WriteString(`([^\/]+\/)*`)
				break
			}
			r.WriteString(`[^\/]*`)
		case '.', '+', '?', '^', '$', '{', '}', '(', ')', '|', '[', ']', '\\':
			r.WriteByte('\\')
			r.WriteByte(c)
		default:
			r.WriteByte(c)
		}
	}
	r.WriteString("$")
	return regexp.MustCompile(r.String())
}

var relativePathRE = regexp.MustCompile(`^\.{1,2}/`)

var importPathExtRE = regexp.MustCompile(`\.(js|ts|d.ts)$`)

// DeriveImportPath derives an ECMAScript module import path from a file path. For example, the
// path `foo/bar.ts` is transformed into `./foo/bar.js`.
func DeriveImportPath(filename string) string {
	importPath := filename
	if loc := importPathExtRE.FindStringIndex(importPath); loc != nil {
		importPath = importPath[:loc[0]] + ".js"
	}
	if !relativePathRE.MatchString(importPath) {
		importPath = "./" + importPath
	}
	return importPath
}

// MakeImportPathRelative makes an import path relative to the file importing it.
func MakeImportPathRelative(importer, importPath string) string {
	if !relativePathRE.MatchString(importPath) {
		// We don't touch absolute imports, like @bufbuild/protobuf
		return importPath
	}
	a := nonEmpty(strings.Split(strings.TrimPrefix(importer, "./"), "/"))
	if len(a) > 0 {
		a = a[:len(a)-1]
	}
	b := nonEmpty(strings.Split(strings.TrimPrefix(importPath, "./"), "/"))
	matching := 0
	for l := min(len(a), len(b)); matching < l; matching++ {
		if a[matching] != b[matching] {
			break
		}
	}
	a = a[matching:]
	b = b[matching:]
	parts := make([]string, 0, len(a)+len(b))
	for range a {
		parts = append(parts, "..")
	}
	parts = append(parts, b...)
	c := strings.Join(parts, "/")
	if relativePathRE.MatchString(c) {
		return c
	}
	return "./" + c
}

func nonEmpty(parts []string) []string {
	out := parts[:0]
	for _, p := range parts {
		if p != "" {
			out = append(out, p)
		}
	}
	return out
}
