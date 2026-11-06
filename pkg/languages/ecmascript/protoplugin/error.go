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

// Package protoplugin is a port of @bufbuild/protoplugin (protobuf-es), the framework for
// ECMAScript code generator plugins: parameter parsing, the schema, generated files with
// automatic imports, file preambles and JSDoc comments.
//
// Ported from packages/protoplugin/src of protobuf-es v2.17.0. Each file names its upstream source.
// The TypeScript-compiler based transpilation (transpile.ts) is not ported.
package protoplugin

import (
	"errors"
	"fmt"
)

// Ported from packages/protoplugin/src/error.ts.

// PluginOptionError reports an invalid plugin option.
type PluginOptionError struct {
	msg string
}

func (e *PluginOptionError) Error() string { return e.msg }

// NewPluginOptionError creates an error for the given raw option, with an optional reason.
func NewPluginOptionError(option string, reason error) *PluginOptionError {
	detail := ""
	if reason != nil {
		detail = reason.Error()
	}
	if detail != "" {
		return &PluginOptionError{fmt.Sprintf("invalid option \"%s\": %s", option, detail)}
	}
	return &PluginOptionError{fmt.Sprintf("invalid option \"%s\"", option)}
}

// IsPluginOptionError reports whether err is a PluginOptionError.
func IsPluginOptionError(err error) bool {
	var e *PluginOptionError
	return errors.As(err, &e)
}
