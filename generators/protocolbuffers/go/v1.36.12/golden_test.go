package golang

import (
	"testing"

	"github.com/exa-pub/protoc-gen-uni/internal/goldentest"
)

func TestGolden(t *testing.T) {
	goldentest.Run(t, Generator)
}
