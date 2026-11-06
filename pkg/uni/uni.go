// Package uni is the core of protoc-gen-uni: it selects a generator by the uni-name and
// uni-version parameters and passes the request with the remaining parameters to it.
package uni

import (
	"fmt"
	"io"
	"slices"
	"strings"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/pluginpb"
)

// Parameter keys consumed by protoc-gen-uni itself.
const (
	ParamName    = "uni-name"
	ParamVersion = "uni-version"
)

// Generator is a port of one upstream plugin at one exact version.
type Generator struct {
	// Name is the plugin name as on buf.build: "<owner>/<plugin>", e.g. "bufbuild/es".
	Name string
	// Version is the exact upstream version with the "v" prefix, e.g. "v2.17.0".
	Version string
	// Generate handles a request whose Parameter no longer contains uni-* options.
	// Generation errors must be reported in CodeGeneratorResponse.Error, as upstream does.
	Generate func(req *pluginpb.CodeGeneratorRequest) *pluginpb.CodeGeneratorResponse
}

// SplitParameter extracts uni-name and uni-version from a protoc parameter string.
// The remaining comma-separated elements are returned unchanged and in their original order:
// some plugins echo the parameter verbatim into generated files.
func SplitParameter(parameter string) (name, version, rest string, err error) {
	if parameter == "" {
		return "", "", "", nil
	}
	var kept []string
	var seenName, seenVersion bool
	for _, elem := range strings.Split(parameter, ",") {
		key, value, _ := strings.Cut(elem, "=")
		switch strings.TrimSpace(key) {
		case ParamName:
			if seenName {
				return "", "", "", fmt.Errorf("protoc-gen-uni: duplicate parameter %q", ParamName)
			}
			seenName, name = true, strings.TrimSpace(value)
		case ParamVersion:
			if seenVersion {
				return "", "", "", fmt.Errorf("protoc-gen-uni: duplicate parameter %q", ParamVersion)
			}
			seenVersion, version = true, strings.TrimSpace(value)
		default:
			kept = append(kept, elem)
		}
	}
	return name, version, strings.Join(kept, ","), nil
}

// Lookup finds the generator with the exact name and version.
func Lookup(gens []Generator, name, version string) (Generator, error) {
	if name == "" || version == "" {
		return Generator{}, fmt.Errorf("protoc-gen-uni: parameters %s=<owner>/<plugin> and %s=<version> are required, e.g. %s=bufbuild/es,%s=v2.17.0",
			ParamName, ParamVersion, ParamName, ParamVersion)
	}
	var versions []string
	for _, g := range gens {
		if g.Name != name {
			continue
		}
		if g.Version == version {
			return g, nil
		}
		versions = append(versions, g.Version)
	}
	if len(versions) == 0 {
		return Generator{}, fmt.Errorf("protoc-gen-uni: unknown generator %q, available: %s", name, strings.Join(Names(gens), ", "))
	}
	slices.Sort(versions)
	return Generator{}, fmt.Errorf("protoc-gen-uni: unknown version %q of %q, available: %s", version, name, strings.Join(versions, ", "))
}

// Names returns the sorted unique generator names.
func Names(gens []Generator) []string {
	var names []string
	for _, g := range gens {
		names = append(names, g.Name)
	}
	slices.Sort(names)
	return slices.Compact(names)
}

// Run dispatches req to the generator selected by its uni-* parameters.
// req.Parameter is replaced with the parameter string without uni-* options.
func Run(req *pluginpb.CodeGeneratorRequest, gens []Generator) *pluginpb.CodeGeneratorResponse {
	name, version, rest, err := SplitParameter(req.GetParameter())
	if err != nil {
		return errorResponse(err)
	}
	g, err := Lookup(gens, name, version)
	if err != nil {
		return errorResponse(err)
	}
	if rest == "" {
		req.Parameter = nil
	} else {
		req.Parameter = proto.String(rest)
	}
	return g.Generate(req)
}

// Main reads a CodeGeneratorRequest from r, runs it and writes the response to w.
func Main(r io.Reader, w io.Writer, gens []Generator) error {
	in, err := io.ReadAll(r)
	if err != nil {
		return fmt.Errorf("protoc-gen-uni: read request: %w", err)
	}
	req := &pluginpb.CodeGeneratorRequest{}
	if err := proto.Unmarshal(in, req); err != nil {
		return fmt.Errorf("protoc-gen-uni: parse request: %w", err)
	}
	out, err := proto.Marshal(Run(req, gens))
	if err != nil {
		return fmt.Errorf("protoc-gen-uni: marshal response: %w", err)
	}
	if _, err := w.Write(out); err != nil {
		return fmt.Errorf("protoc-gen-uni: write response: %w", err)
	}
	return nil
}

func errorResponse(err error) *pluginpb.CodeGeneratorResponse {
	return &pluginpb.CodeGeneratorResponse{Error: proto.String(err.Error())}
}
