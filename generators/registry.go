// Package generators lists all generators bundled into protoc-gen-uni.
package generators

import (
	es_v2_17_0 "github.com/exa-pub/protoc-gen-uni/generators/bufbuild/es/v2.17.0"
	golang_v1_36_12 "github.com/exa-pub/protoc-gen-uni/generators/protocolbuffers/go/v1.36.12"
	"github.com/exa-pub/protoc-gen-uni/pkg/uni"
)

// All is every bundled generator. Keep it sorted by name, then version.
var All = []uni.Generator{
	es_v2_17_0.Generator,
	golang_v1_36_12.Generator,
}
