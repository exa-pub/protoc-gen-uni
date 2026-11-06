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

// Ported from packages/protoplugin/src/map-imports.ts.

// compileMapImports compiles the patterns of the plugin option map_imports.
func compileMapImports(mapImports []PatternTarget) []compiledPattern {
	out := make([]compiledPattern, 0, len(mapImports))
	for _, mi := range mapImports {
		out = append(out, compiledPattern{globToRegExp(mi.Pattern), strings.TrimSuffix(mi.Target, "/")})
	}
	return out
}

// mapImportTarget returns the target for the given Protobuf file path, or "" if none of the
// patterns match.
func mapImportTarget(protoFileName string, mapImports []compiledPattern) (string, bool) {
	for _, mi := range mapImports {
		if mi.pattern.MatchString(protoFileName) {
			return mi.target, true
		}
	}
	return "", false
}

func globToRegExp(glob string) *regexp.Regexp {
	var r strings.Builder
	r.WriteString("^")
	for i := 0; i < len(glob); i++ {
		switch c := glob[i]; c {
		case '*':
			if i+1 < len(glob) && glob[i+1] == '*' {
				if i+2 < len(glob) && glob[i+2] == '/' {
					i += 2
					r.WriteString(`([^\/]+\/)*`)
					break
				}
				i++
				r.WriteString(`.*`)
				break
			}
			r.WriteString(`[^\/]*`)
		case '/':
			if i == len(glob)-1 {
				// A trailing slash matches everything in the directory.
				r.WriteString(`\/.*`)
				break
			}
			r.WriteString(`\/`)
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
