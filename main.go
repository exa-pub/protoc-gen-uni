// Command protoc-gen-uni is a single protoc plugin that bundles byte-exact Go ports of upstream
// plugins. The generator is selected with the uni-name and uni-version parameters, all other
// parameters are passed to it unchanged:
//
//	opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
package main

import (
	"fmt"
	"io"
	"os"

	"github.com/exa-pub/protoc-gen-uni/generators"
	"github.com/exa-pub/protoc-gen-uni/pkg/uni"
)

// version is set at build time: -ldflags "-X main.version=vX.Y.Z".
var version = "dev"

func main() {
	if err := run(os.Args[1:], os.Stdin, os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func run(args []string, r io.Reader, w io.Writer) error {
	if len(args) == 1 && args[0] == "--version" {
		_, err := fmt.Fprintln(w, version)
		return err
	}
	if len(args) > 0 {
		return fmt.Errorf("protoc-gen-uni: unexpected arguments %q; this is a protoc plugin, run it via protoc or buf", args)
	}
	return uni.Main(r, w, generators.All)
}
