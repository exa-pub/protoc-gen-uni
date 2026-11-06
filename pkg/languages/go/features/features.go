// Copyright 2024 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package features resolves the Go-specific edition features (pb.go) of
// descriptors with the public protoreflect API.
//
// protoc-gen-go reads them from internal/filedesc (EditionFeatures), which is
// populated by reflect/protodesc when it builds descriptors. This package
// reimplements the same resolution, ported from google.golang.org/protobuf
// v1.36.12:
//   - internal/editiondefaults (the embedded editions_defaults.binpb),
//   - reflect/protodesc/editions.go (getFeatureSetFor, mergeEditionFeatures),
//   - internal/filedesc/presence.go (UsePresenceForField).
package features

import (
	_ "embed"
	"fmt"
	"sync"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/gofeaturespb"

	"github.com/exa-pub/protoc-gen-uni/pkg/languages/go/genid"
)

// Defaults is the binary FeatureSetDefaults (descriptor.proto and
// go_features.proto) compiled into google.golang.org/protobuf v1.36.12
// (internal/editiondefaults/editions_defaults.binpb).
//
//go:embed editions_defaults.binpb
var Defaults []byte

// Go are the resolved pb.go features of a descriptor, the subset of
// filedesc.EditionFeatures used by Go code generators.
type Go struct {
	// LegacyUnmarshalJSONEnum determines if the plugin generates the
	// (deprecated) UnmarshalJSON method for enums.
	LegacyUnmarshalJSONEnum bool
	// APILevel is the pb.GoFeatures.APILevel value.
	APILevel int
	// StripEnumPrefix is the pb.GoFeatures.StripEnumPrefix value.
	StripEnumPrefix int
}

var (
	defaultsOnce   sync.Once
	defaultsParsed *descriptorpb.FeatureSetDefaults
	defaultsErr    error
	defaultsMu     sync.Mutex
	defaultsCache  = map[descriptorpb.Edition]Go{}
)

func parsedDefaults() (*descriptorpb.FeatureSetDefaults, error) {
	defaultsOnce.Do(func() {
		defaultsParsed = &descriptorpb.FeatureSetDefaults{}
		if err := proto.Unmarshal(Defaults, defaultsParsed); err != nil {
			defaultsErr = fmt.Errorf("unmarshal editions defaults: %w", err)
		}
	})
	return defaultsParsed, defaultsErr
}

// ForEdition returns the default pb.go features of an edition
// (protodesc.getFeatureSetFor merged into an empty feature set).
func ForEdition(edition descriptorpb.Edition) (Go, error) {
	defaultsMu.Lock()
	defer defaultsMu.Unlock()
	if g, ok := defaultsCache[edition]; ok {
		return g, nil
	}
	defaults, err := parsedDefaults()
	if err != nil {
		return Go{}, err
	}
	if (defaults.GetMinimumEdition() > edition || defaults.GetMaximumEdition() < edition) && edition != descriptorpb.Edition_EDITION_UNSTABLE {
		return Go{}, fmt.Errorf("unsupported edition %v", edition)
	}
	if len(defaults.GetDefaults()) == 0 {
		return Go{}, fmt.Errorf("no edition defaults")
	}
	fsed := defaults.GetDefaults()[0]
	for _, def := range defaults.GetDefaults() {
		if def.GetEdition() <= edition {
			fsed = def
		} else {
			break
		}
	}
	fs := proto.Clone(fsed.GetFixedFeatures()).(*descriptorpb.FeatureSet)
	proto.Merge(fs, fsed.GetOverridableFeatures())
	g := Merge(Go{}, fs)
	defaultsCache[edition] = g
	return g, nil
}

// Edition returns the edition of a file descriptor, as protodesc does.
func Edition(fd protoreflect.FileDescriptor) descriptorpb.Edition {
	switch fd.Syntax() {
	case protoreflect.Proto3:
		return descriptorpb.Edition_EDITION_PROTO3
	case protoreflect.Editions:
		if e, ok := fd.(interface{ Edition() int32 }); ok {
			return descriptorpb.Edition(e.Edition())
		}
		return descriptorpb.Edition_EDITION_UNKNOWN
	default:
		return descriptorpb.Edition_EDITION_PROTO2
	}
}

// Resolve returns the pb.go features of d: the edition defaults of its file,
// overridden by the features set on the file and on every enclosing
// declaration down to d itself.
func Resolve(d protoreflect.Descriptor) (Go, error) {
	if fd, ok := d.(protoreflect.FileDescriptor); ok {
		g, err := ForEdition(Edition(fd))
		if err != nil {
			return Go{}, err
		}
		return Merge(g, featureSetOf(fd)), nil
	}
	parent := d.Parent()
	if parent == nil {
		parent = d.ParentFile()
	}
	if parent == nil {
		return Go{}, fmt.Errorf("descriptor %v has no parent", d.FullName())
	}
	g, err := Resolve(parent)
	if err != nil {
		return Go{}, err
	}
	return Merge(g, featureSetOf(d)), nil
}

func featureSetOf(d protoreflect.Descriptor) *descriptorpb.FeatureSet {
	switch opts := d.Options().(type) {
	case *descriptorpb.FileOptions:
		return opts.GetFeatures()
	case *descriptorpb.MessageOptions:
		return opts.GetFeatures()
	case *descriptorpb.FieldOptions:
		return opts.GetFeatures()
	case *descriptorpb.OneofOptions:
		return opts.GetFeatures()
	case *descriptorpb.EnumOptions:
		return opts.GetFeatures()
	case *descriptorpb.EnumValueOptions:
		return opts.GetFeatures()
	case *descriptorpb.ServiceOptions:
		return opts.GetFeatures()
	case *descriptorpb.MethodOptions:
		return opts.GetFeatures()
	}
	return nil
}

// Merge overrides parent with the pb.go features set in child, like the Go
// part of protodesc.mergeEditionFeatures.
func Merge(parent Go, child *descriptorpb.FeatureSet) Go {
	if child == nil {
		return parent
	}
	// We must not use proto.GetExtension(child, gofeaturespb.E_Go)
	// because that only works for messages we generated, but not for
	// dynamicpb messages. See golang/protobuf#1669.
	goFeatures := child.ProtoReflect().Get(gofeaturespb.E_Go.TypeDescriptor())
	if !goFeatures.IsValid() {
		return parent
	}
	gf, ok := goFeatures.Interface().(protoreflect.Message)
	if !ok {
		return parent
	}
	fields := gf.Descriptor().Fields()

	if fd := fields.ByNumber(genid.GoFeatures_LegacyUnmarshalJsonEnum_field_number); fd != nil &&
		!fd.IsList() &&
		fd.Kind() == protoreflect.BoolKind &&
		gf.Has(fd) {
		parent.LegacyUnmarshalJSONEnum = gf.Get(fd).Bool()
	}

	if fd := fields.ByNumber(genid.GoFeatures_StripEnumPrefix_field_number); fd != nil &&
		!fd.IsList() &&
		fd.Kind() == protoreflect.EnumKind &&
		gf.Has(fd) {
		parent.StripEnumPrefix = int(gf.Get(fd).Enum())
	}

	if fd := fields.ByNumber(genid.GoFeatures_ApiLevel_field_number); fd != nil &&
		!fd.IsList() &&
		fd.Kind() == protoreflect.EnumKind &&
		gf.Has(fd) {
		parent.APILevel = int(gf.Get(fd).Enum())
	}

	return parent
}

// UsePresenceForField reports whether the presence bitmap should be used for
// the specified field (filedesc.UsePresenceForField).
func UsePresenceForField(fd protoreflect.FieldDescriptor) (usePresence, canBeLazy bool) {
	switch {
	case fd.ContainingOneof() != nil && !fd.ContainingOneof().IsSynthetic():
		// Oneof fields never use the presence bitmap.
		//
		// Synthetic oneofs are an exception: Those are used to implement proto3
		// optional fields and hence should follow non-oneof field semantics.
		return false, false

	case fd.IsMap():
		// Map-typed fields never use the presence bitmap.
		return false, false

	case fd.Kind() == protoreflect.MessageKind || fd.Kind() == protoreflect.GroupKind:
		// Lazy fields always use the presence bitmap (only messages can be lazy).
		isLazy := IsLazy(fd)
		return isLazy, isLazy

	default:
		// If the field has presence, use the presence bitmap.
		return fd.HasPresence(), false
	}
}

// IsLazy reports whether the field is lazy (filedesc Field.IsLazy, which
// protodesc sets from google.protobuf.FieldOptions.lazy).
func IsLazy(fd protoreflect.FieldDescriptor) bool {
	if l, ok := fd.(interface{ IsLazy() bool }); ok {
		return l.IsLazy()
	}
	opts, _ := fd.Options().(*descriptorpb.FieldOptions)
	return opts.GetLazy()
}
