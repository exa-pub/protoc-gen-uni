package main

import (
	"bytes"
	"testing"
)

func TestRun(t *testing.T) {
	tests := []struct {
		name string
		args []string
		want string
	}{
		{name: "hello", args: nil, want: "Hello, world!\n"},
		{name: "version", args: []string{"--version"}, want: version + "\n"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var out bytes.Buffer
			if err := run(tt.args, &out); err != nil {
				t.Fatalf("run(%q): %v", tt.args, err)
			}
			if got := out.String(); got != tt.want {
				t.Errorf("run(%q) = %q, want %q", tt.args, got, tt.want)
			}
		})
	}
}
