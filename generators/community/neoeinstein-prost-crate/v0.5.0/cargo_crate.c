/* Port of src/generator/cargo_crate.rs of protoc-gen-prost-crate 0.5.0
 * (https://github.com/neoeinstein/protoc-gen-prost, Apache-2.0).
 *
 * Upstream reads the Cargo.toml template (gen_crate[=<path>], default "Cargo.toml") from the file
 * system of the plugin and copies it without the lines from `@@protoc_deletion_point(features)` to
 * `@@protoc_insertion_point(features)`. protoc-gen-uni reads no files besides the request, so
 * gen_crate is not supported. The buf.build remote plugin fails with "No such file or directory
 * (os error 2)" for a template outside its container, but reads the files of its container
 * (gen_crate=/dev/null gives an empty Cargo.toml), so no fixed answer matches it. */

/* CargoCrateGenerator::generate: the error. */
COLD static const char *cargo_crate_generate(const char *manifest_template_path)
{
	return xcat("unsupported by protoc-gen-uni: gen_crate (reads the Cargo.toml template ",
		    quote_json(str_c(manifest_template_path ? manifest_template_path : "Cargo.toml")),
		    " from the file system of the plugin)");
}
