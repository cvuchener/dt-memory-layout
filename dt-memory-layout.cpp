/*
 * Copyright 2023 Clement Vuchener
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <filesystem>
#include <iostream>
#include <iomanip>
#include <algorithm>

#include <format>
#include <ranges>

#include <dfs/Structures.h>
#include <dfs/ABI.h>
#include <dfs/MemoryLayout.h>
#include <dfs/Path.h>
#include <dfs/Pointer.h>
using namespace dfs;

#include <pugixml.hpp>
using namespace pugi;

namespace fs = std::filesystem;

struct hex_value
{
	std::size_t value;
};

template <>
struct std::formatter<hex_value>
{
	template <typename ParseContext>
	constexpr ParseContext::iterator parse(ParseContext &ctx)
	{
		return ctx.begin();
	}

	template <typename FormatContext>
	constexpr FormatContext::iterator format(hex_value v, FormatContext &ctx) const
	{
		int w = 4;
		if (v.value >> 16)
			w = 8;
		//if (v.value >> 32)
		//	w = 16;
		return format_to(ctx.out(), "{:#0{}x}", v.value, w+2);
	}
};

static void print_value(std::string_view name, std::size_t value)
{
	std::cout << std::format("{}={}\n", name, hex_value{value});
}

static bool print_section(const Structures &structures, const Structures::VersionInfo &version,
			  const ABI &abi, const MemoryLayout &layout, const xml_node element)
{
	bool ok = true;
	for (auto child: element.children()) {
		if (child.type() != node_element)
			continue;
		std::string_view entry_name = child.attribute("name").value();

		const Compound *type = nullptr;
		if (auto type_attr = child.attribute("type")) {
			std::string_view type_name = type_attr.value();
			try {
				type = structures.findCompound(parse_path(type_name));
			}
			catch (std::exception &e) {
				std::cerr << std::format("type {} not found for entry {}: {}.\n",
						type_attr.value(),
						entry_name,
						e.what());
				ok = false;
				continue;
			}
			if (!type) {
				std::cerr << std::format("type {} not found for entry {}.\n",
						type_attr.value(),
						entry_name);
				ok = false;
				continue;
			}
		}

		std::string_view name = child.name();
		if (name == "offset") {
			if (!type) {
				std::cerr << std::format("offset {} need a type.\n", entry_name);
				ok = false;
				continue;
			}
			std::string_view member = child.attribute("member").value();
			try {
				auto [member_type, offset] = layout.getOffset(*type, parse_path(member));
				print_value(entry_name, offset);
			}
			catch (std::exception &e) {
				std::cerr << std::format("Failed to get member {} offset for {}: {}.\n", member, entry_name, e.what());
				ok = false;
				continue;
			}
		}
		else if (name == "size") {
			if (!type) {
				std::cerr << std::format("size {} need a type.\n", entry_name);
				ok = false;
				continue;
			}
			auto it = layout.type_info.find(type);
			if (it == layout.type_info.end()) {
				std::cerr << std::format("Missing type info for size {}.\n", entry_name);
				ok = false;
				continue;
			}
			print_value(entry_name, it->second.size);
		}
		else if (name == "vmethod") {
			if (!type) {
				std::cerr << std::format("vmethod {} need a type.\n", entry_name);
				ok = false;
				continue;
			}
			std::string_view method = child.attribute("method").value();
			auto vtable_index = type->methodIndex(method);
			if (vtable_index == -1) {
				std::cerr << std::format("Method {} not found for vmethod {}.\n", method, entry_name);
				ok = false;
			}
			else {
				print_value(entry_name, vtable_index * abi.pointer.size);
			}
		}
		else if (name == "value") {
			int value = 0;
			if (auto enum_name = child.attribute("enum")) {
				if (auto enum_type = structures.findEnum(enum_name.value())) {
					std::string_view value_name = child.attribute("value").value();
					auto value_it = enum_type->values.find(value_name);
					if (value_it != enum_type->values.end())
						value = value_it->second.value;
					else {
						std::cerr << std::format("Unknown enum value {} in {}.\n", value_name, enum_name.value());
						ok = false;
						continue;
					}
				}
				else {
					std::cerr << std::format("Unknown enum {}.\n", enum_name.value());
					ok = false;
					continue;
				}
			}
			else
				value = child.attribute("value").as_int();
			print_value(entry_name, value);
		}
		else if (name == "global") {
			std::string_view object = child.attribute("object").value();
			try {
				auto ptr = Pointer::fromGlobal(structures, version, layout, parse_path(object));
				print_value(entry_name, ptr.address);
			}
			catch (std::exception &e) {
				std::cerr << std::format("Global object {}: {}\n", object, e.what());
				ok = false;
			}
		}
		else if (name == "vtable") {
			std::string_view type = child.attribute("type").value();
			auto it = version.vtables_addresses.find(type);
			if (it != version.vtables_addresses.end()) {
				print_value(entry_name, it->second);
			}
			else {
				std::cerr << std::format("Failed to find vtable for {}.\n", entry_name);
				ok = false;
			}
		}
		else {
			std::cerr << std::format("Invalid tag name: {}.\n", name);
			ok = false;
		}
	}
	return ok;
}

static bool print_flag_array(const Structures &structures, const xml_node element)
{
	std::string_view bitfield_name = element.attribute("bitfield").value();
	auto bitfield = structures.findBitfield(bitfield_name);
	if (!bitfield) {
		std::cerr << std::format("Unknown bitfield {}.\n", bitfield_name);
		return false;
	}

	bool ok = true;
	std::vector<std::tuple<std::string_view, std::size_t>> values;
	for (auto child: element.children()) {
		if (child.type() != node_element)
			continue;
		std::string_view name = child.name();
		if (name != "flag") {
			std::cerr << std::format("invalid tagname {} in flag-array.\n", name);
			ok = false;
			continue;
		}
		std::string_view flags = child.attribute("flags").value();
		int value = 0;
		for (auto flag_name_range: flags | std::views::split('|')) {
			auto flag_name = std::string_view(std::begin(flag_name_range), std::end(flag_name_range));
			auto flag_it = std::ranges::find(bitfield->flags, flag_name, &Bitfield::Flag::name);
			if (flag_it != bitfield->flags.end()) {
				if (flag_it->count != 1) {
					std::cerr << std::format("{} is not a single bit flag.\n", flag_name);
					ok = false;
					continue;
				}
				value |= 1 << flag_it->offset;
			}
			else {
				std::cerr << std::format("Unknown flag value {} in {}.\n", flag_name, bitfield_name);
				ok = false;
				continue;
			}
		}
		values.emplace_back(child.attribute("name").value(), value);
	}

	std::cout << std::format("size={}\n", values.size());
	for (unsigned int i = 0; i < values.size(); ++i) {
		std::cout << std::format("{}\\name=\"{}\"\n", i+1, std::get<0>(values[i]));
		std::cout << std::format("{}\\value={:#010x}\n", i+1, std::get<1>(values[i]));
	}
	return ok;
}

int main(int argc, char *argv[]) try
{
	if (argc != 4) {
		std::cerr << std::format("Usage: {} df_structures_path version_name memory_layout_xml\n", argv[0]);
		return EXIT_FAILURE;
	}
	fs::path df_structures_path = argv[1];
	const char *version_name = argv[2];
	fs::path memory_layout_xml = argv[3];

	Structures structures(df_structures_path);

	auto version = structures.versionByName(version_name);
	if (!version) {
		std::cerr << std::format("Version \"{}\" not found\n", version_name);
		std::cerr << std::format("Available versions are:\n");
		for (const auto &version: structures.allVersions())
			std::cerr << std::format(" - {}\n", version.version_name);
		return EXIT_FAILURE;
	}

	const ABI &abi = ABI::fromVersionName(version_name);

	MemoryLayout layout(structures, abi);

	std::cout << std::format("[info]\n");
	if (version->id.size() < 4) {
		std::cerr << std::format("Invalid version id, size is too small: {}\n", version->id.size());
		return EXIT_FAILURE;
	}
	std::cout << std::format("checksum=0x{:02x}{:02x}{:02x}{:02x}\n",
			version->id[0],
			version->id[1],
			version->id[2],
			version->id[3]);
	std::cout << std::format("version_name={}\n", version_name);
	std::cout << std::format("complete=true\n");
	std::cout << std::format("\n");

	xml_document doc;
	auto res = doc.load_file(memory_layout_xml.c_str());
	if (!res) {
		std::cerr << std::format("Failed to parse memory layout xml: {}\n", res.description());
		return EXIT_FAILURE;
	}

	bool failed = false;
	for (auto element: doc.document_element().children()) {
		if (element.type() != node_element)
			continue;
		std::string_view name = element.name();

		std::cout << std::format("[{}]\n", element.attribute("name").value());
		if (name == "section") {
			if (!print_section(structures, *version, abi, layout, element))
				failed = true;
		}
		else if (name == "flag-array") {
			if (!print_flag_array(structures, element))
				failed = true;
		}
		else {
			std::cerr << std::format("Ignoring unknown tag name: {}\n", name);
			failed = true;
			continue;
		}
		std::cout << std::endl;
	}
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
catch (std::exception &e) {
	std::cerr << std::format("Could not load structures: {}\n", e.what());
	return EXIT_FAILURE;
}
