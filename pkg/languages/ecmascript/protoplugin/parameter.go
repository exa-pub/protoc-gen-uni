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

import "strings"

// Ported from packages/protoplugin/src/parameter.ts.

// Target is a value of the plugin option `target`: "js", "ts" or "dts".
type Target string

// Targets.
const (
	TargetJs  Target = "js"
	TargetTs  Target = "ts"
	TargetDts Target = "dts"
)

// ImportExtension is a value of the plugin option `import_extension`: "none", "js" or "ts".
type ImportExtension string

// JSImportStyle is a value of the plugin option `js_import_style`.
type JSImportStyle string

// JS import styles.
const (
	JSImportStyleModule         JSImportStyle = "module"
	JSImportStyleLegacyCommonJS JSImportStyle = "legacy_commonjs"
)

// PatternTarget is a pattern with a target, used by rewrite_imports and map_imports.
type PatternTarget struct {
	Pattern string
	Target  string
}

// EcmaScriptPluginOptions are the standard plugin options that every ECMAScript plugin supports.
type EcmaScriptPluginOptions struct {
	Targets            []Target
	ImportExtension    ImportExtension
	JSImportStyle      JSImportStyle
	TsNocheck          bool
	ElidePluginVersion bool
	KeepEmptyFiles     bool
	BootstrapWkt       bool
	MapImports         []PatternTarget
	RewriteImports     []PatternTarget
}

// RawOption is a plugin option that is not handled by the framework.
type RawOption struct {
	Key   string
	Value string
}

// ParsedParameter is the result of ParseParameter.
type ParsedParameter[T any] struct {
	Parsed EcmaScriptPluginOptions
	// Extra are the options parsed by the plugin.
	Extra T
	// Sanitized is the parameter without rewrite_imports, printed in the file preamble.
	Sanitized string
}

// ParseParameter parses the plugin parameter. Options unknown to the framework are passed to
// parseExtraOptions; if it is nil, they are rejected.
func ParseParameter[T any](parameter string, parseExtraOptions func([]RawOption) (T, error)) (*ParsedParameter[T], error) {
	opts := EcmaScriptPluginOptions{
		Targets:         []Target{TargetJs, TargetDts},
		ImportExtension: "none",
		JSImportStyle:   JSImportStyleModule,
	}
	var extra []RawOption
	var extraRaw []string
	var rawParameters []string
	parseBool := func(raw, value string, dst *bool) error {
		switch value {
		case "true", "1":
			*dst = true
		case "false", "0":
			*dst = false
		default:
			return NewPluginOptionError(raw, nil)
		}
		return nil
	}
	for _, p := range splitParameter(parameter) {
		key, value, raw := p.key, p.value, p.raw
		sanitize := false
		switch key {
		case "target":
			opts.Targets = nil
			for _, rawTarget := range strings.Split(value, "+") {
				switch t := Target(rawTarget); t {
				case TargetJs, TargetTs, TargetDts:
					if !containsTarget(opts.Targets, t) {
						opts.Targets = append(opts.Targets, t)
					}
				default:
					return nil, NewPluginOptionError(raw, nil)
				}
			}
		case "ts_nocheck":
			if err := parseBool(raw, value, &opts.TsNocheck); err != nil {
				return nil, err
			}
		case "elide_plugin_version":
			if err := parseBool(raw, value, &opts.ElidePluginVersion); err != nil {
				return nil, err
			}
		case "bootstrap_wkt":
			if err := parseBool(raw, value, &opts.BootstrapWkt); err != nil {
				return nil, err
			}
		case "rewrite_imports":
			i := strings.Index(value, ":")
			if i < 0 {
				return nil, NewPluginOptionError(raw, errString("must be in the form of <pattern>:<target>"))
			}
			opts.RewriteImports = append(opts.RewriteImports, PatternTarget{value[:i], value[i+1:]})
			// rewrite_imports can be noisy and is more of an implementation detail
			// so we strip it out of the preamble
			sanitize = true
		case "map_imports":
			i := strings.Index(value, ":")
			if i < 1 || i == len(value)-1 {
				return nil, NewPluginOptionError(raw, errString("must be in the form of <pattern>:<target>"))
			}
			opts.MapImports = append(opts.MapImports, PatternTarget{value[:i], value[i+1:]})
		case "import_extension":
			switch value {
			case "none", "":
				opts.ImportExtension = "none"
			case "js", ".js":
				opts.ImportExtension = "js"
			case "ts", ".ts":
				opts.ImportExtension = "ts"
			default:
				return nil, NewPluginOptionError(raw, nil)
			}
		case "js_import_style":
			switch value {
			case "module", "legacy_commonjs":
				opts.JSImportStyle = JSImportStyle(value)
			default:
				return nil, NewPluginOptionError(raw, nil)
			}
		case "keep_empty_files":
			if err := parseBool(raw, value, &opts.KeepEmptyFiles); err != nil {
				return nil, err
			}
		default:
			if parseExtraOptions == nil {
				return nil, NewPluginOptionError(raw, nil)
			}
			extra = append(extra, RawOption{key, value})
			extraRaw = append(extraRaw, raw)
		}
		if !sanitize {
			rawParameters = append(rawParameters, raw)
		}
	}
	result := &ParsedParameter[T]{
		Parsed:    opts,
		Sanitized: strings.Join(rawParameters, ","),
	}
	if parseExtraOptions != nil {
		parsed, err := parseExtraOptions(extra)
		if err != nil {
			return nil, NewPluginOptionError(strings.Join(extraRaw, ","), err)
		}
		result.Extra = parsed
	}
	return result, nil
}

type errString string

func (e errString) Error() string { return string(e) }

func containsTarget(targets []Target, t Target) bool {
	for _, x := range targets {
		if x == t {
			return true
		}
	}
	return false
}

type parameterPart struct {
	key, value, raw string
}

func splitParameter(parameter string) []parameterPart {
	if parameter == "" {
		return nil
	}
	var parts []parameterPart
	for _, raw := range strings.Split(parameter, ",") {
		key, value, _ := strings.Cut(raw, "=")
		parts = append(parts, parameterPart{key, value, raw})
	}
	return parts
}
