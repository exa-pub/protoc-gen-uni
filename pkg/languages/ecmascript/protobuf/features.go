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

package protobuf

import (
	"fmt"

	"google.golang.org/protobuf/types/descriptorpb"
)

// Ported from packages/protobuf/src/registry.ts (featureDefaults, resolveFeature).

type feature int

const (
	featFieldPresence feature = iota
	featEnumType
	featRepeatedFieldEncoding
	featUtf8Validation
	featMessageEncoding
	featJSONFormat
	featEnforceNamingStyle
	featDefaultSymbolVisibility
)

// featureDefaults are the feature defaults per edition, as embedded in @bufbuild/protobuf.
var featureDefaults = map[descriptorpb.Edition][8]int32{
	EditionProto2: {1, 2, 2, 3, 1, 2, 2, 1},
	EditionProto3: {2, 1, 1, 2, 1, 1, 2, 1},
	Edition2023:   {1, 1, 1, 2, 1, 1, 2, 1},
	Edition2024:   {1, 1, 1, 2, 1, 1, 1, 2},
	Edition2026:   {1, 1, 1, 2, 1, 1, 3, 4},
}

// featureRef is a *DescFile or a *DescMessage: an element whose features are inherited.
type featureRef interface {
	Desc
	features() *descriptorpb.FeatureSet
}

func (d *DescFile) features() *descriptorpb.FeatureSet    { return d.Proto.GetOptions().GetFeatures() }
func (d *DescMessage) features() *descriptorpb.FeatureSet { return d.Proto.GetOptions().GetFeatures() }

func featureValue(fs *descriptorpb.FeatureSet, name feature) int32 {
	switch name {
	case featFieldPresence:
		return int32(fs.GetFieldPresence())
	case featEnumType:
		return int32(fs.GetEnumType())
	case featRepeatedFieldEncoding:
		return int32(fs.GetRepeatedFieldEncoding())
	case featUtf8Validation:
		return int32(fs.GetUtf8Validation())
	case featMessageEncoding:
		return int32(fs.GetMessageEncoding())
	case featJSONFormat:
		return int32(fs.GetJsonFormat())
	case featEnforceNamingStyle:
		return int32(fs.GetEnforceNamingStyle())
	case featDefaultSymbolVisibility:
		return int32(fs.GetDefaultSymbolVisibility())
	}
	return 0
}

// resolveFeature resolves a feature of an element with the given own features, inheriting from
// parent (a message or file).
func resolveFeature(name feature, own *descriptorpb.FeatureSet, parent featureRef) int32 {
	if own != nil {
		if v := featureValue(own, name); v != 0 {
			return v
		}
	}
	for {
		if v := featureValue(parent.features(), name); v != 0 {
			return v
		}
		switch p := parent.(type) {
		case *DescMessage:
			if p.Parent != nil {
				parent = p.Parent
			} else {
				parent = p.File
			}
		case *DescFile:
			defaults, ok := featureDefaults[p.Edition]
			if !ok {
				panic(registryError{fmt.Sprintf("feature default for edition %d not found", p.Edition)})
			}
			return defaults[name]
		}
	}
}
