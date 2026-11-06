// Command protoc-gen-uni is a unified code generator for protobuf (work in progress).
package main

import (
	"fmt"
	"io"
	"os"
)

// version is set at build time: -ldflags "-X main.version=vX.Y.Z".
var version = "dev"

func main() {
	if err := run(os.Args[1:], os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func run(args []string, w io.Writer) error {
	if len(args) > 0 && args[0] == "--version" {
		_, err := fmt.Fprintln(w, version)
		return err
	}
	_, err := fmt.Fprintln(w, "Hello, world!")
	return err
}
