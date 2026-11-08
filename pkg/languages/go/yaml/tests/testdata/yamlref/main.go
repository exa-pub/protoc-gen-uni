// yamlref: reference outputs of go.yaml.in/yaml/v3 v3.0.4 for the C port's unit test.
// Input lines: "<mode> <hex>" with mode s (a string scalar, as gnostic's NewScalarNodeForString,
// as a mapping value and key), y (yaml parsed into a Node as gnostic-models Any.ToRawInfo does,
// then marshalled under a key), f (a float formatted with %g, as NewScalarNodeForFloat).
// Output lines: hex of the marshalled document, or "E" + hex of the error.
package main

import (
	"bufio"
	"encoding/hex"
	"fmt"
	"os"
	"strconv"
	"strings"

	yaml "go.yaml.in/yaml/v3"
)

func str(s string) *yaml.Node { return &yaml.Node{Kind: yaml.ScalarNode, Tag: "!!str", Value: s} }

func anyNode(s string) *yaml.Node {
	var node yaml.Node
	err := yaml.Unmarshal([]byte(s), &node)
	if err == nil {
		if node.Kind == yaml.DocumentNode {
			return node.Content[0]
		}
		return &node
	}
	return &yaml.Node{Kind: yaml.ScalarNode, Tag: "!!null"}
}

func main() {
	sc := bufio.NewScanner(os.Stdin)
	sc.Buffer(make([]byte, 1<<20), 1<<24)
	for sc.Scan() {
		line := sc.Text()
		mode, h, _ := strings.Cut(line, " ")
		in, err := hex.DecodeString(h)
		if err != nil {
			panic(err)
		}
		var v *yaml.Node
		m := &yaml.Node{Kind: yaml.MappingNode, Content: []*yaml.Node{}}
		switch mode {
		case "s":
			m.Content = append(m.Content, str("key"), str(string(in)), str(string(in)), str("v"))
			seq := &yaml.Node{Kind: yaml.SequenceNode, Content: []*yaml.Node{str(string(in))}}
			m.Content = append(m.Content, str("seq"), seq)
		case "y":
			m.Content = append(m.Content, str("example"), anyNode(string(in)))
			seq := &yaml.Node{Kind: yaml.SequenceNode, Content: []*yaml.Node{anyNode(string(in))}}
			m.Content = append(m.Content, str("enum"), seq)
		case "f":
			f, _ := strconv.ParseFloat(string(in), 64)
			m.Content = append(m.Content, str("maximum"), &yaml.Node{Kind: yaml.ScalarNode, Tag: "!!float", Value: fmt.Sprintf("%g", f)})
		}
		v = &yaml.Node{Kind: yaml.DocumentNode, Content: []*yaml.Node{m}, HeadComment: "Generated with protoc-gen-openapi\nhttps://example.com"}
		out, err := func() (out []byte, err error) {
			defer func() {
				if r := recover(); r != nil {
					err = fmt.Errorf("PANIC %v", r)
				}
			}()
			return yaml.Marshal(v)
		}()
		if err != nil {
			fmt.Println("E" + hex.EncodeToString([]byte(err.Error())))
		} else {
			fmt.Println(hex.EncodeToString(out))
		}
	}
}
