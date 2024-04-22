#!/usr/bin/python3

# Copyright 2020 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB

import sys

def snake_to_camel(str):
  return ''.join(word.title() for word in str.split('_'))

if len(sys.argv) == 2:
  compiler_check = False
  template_file = sys.argv[1]
elif len(sys.argv) == 3:
  compiler_check = sys.argv[1] == "--compiler-check"
  template_file = sys.argv[2]

minimum_version = ""
namespace_name = ""
struct_name = ""
path_name = ""
handle_name = ""

function_types = []
function_members = []
function_initializers = []
static_asserts = []
enums = {}
current_enum = None

with open(template_file) as file:
 for line in file:
  line = line.rstrip('\n')

  if not line or line[0] == '#':
    continue

  if not minimum_version:
    minimum_version = line
  elif not namespace_name:
    namespace_name = line
  elif not struct_name:
    struct_name = line
  elif not path_name:
    path_name = line
  elif not handle_name:
    handle_name = line
  elif line:
    (one, two) = line.split(maxsplit=1)
    if current_enum is None:
        if one != 'enum':
            function_name = one
            function_type = two
            type_name = snake_to_camel(function_name)
            function_types.append("using %s = %s" % (type_name, function_type))
            function_members.append("std::function<%s> %s" % (type_name, function_name))
            function_initializers.append("%s{%s.load<%s>(\"%s\")}" % (function_name, handle_name, type_name, function_name))
            static_asserts.append("static_assert(std::is_same<%s::%s::%s, decltype(::%s)>::value);"
	            % (namespace_name, struct_name, type_name, function_name))
        else:
            (enum_name, enum_type) = two.split(maxsplit=1)
            current_enum = (snake_to_camel(enum_name), enum_type)
            enums[current_enum] = []
    else:
        if two == 'end':
            current_enum = None
            continue
        entry_name = snake_to_camel(one)
        enums[current_enum].append((entry_name, two))
        static_asserts.append("static_assert((%s)%s::%s::%s::%s == %s);"
            % (current_enum[1], namespace_name, struct_name, current_enum[0], entry_name, one))

if compiler_check:
    print(f'#include <functional>')
    print(f'#include <type_traits>')
    print(f'#include <flux/core.h>')
    print(f'struct {namespace_name}\n{{')
    print(f'    struct {struct_name};\n')
    print('};')
else:
    print('#pragma once')

print(f'struct {namespace_name}::{struct_name}\n{{')

[minimum_major, minimum_minor] = minimum_version.split('.')
print(f'    static auto constexpr minimum_version_major={minimum_major};')
print(f'    static auto constexpr minimum_version_minor={minimum_minor};')

for function_type in function_types:
    print(f'    {function_type};')

for ((enum_name, enum_type), values) in enums.items():
    print(f'\n    enum class {enum_name} : {enum_type}\n    {{')
    for (var, val) in values:
        print(f'        {var} = {val},')
    print('    };')

if compiler_check:
    print('};')
    for static_assert in static_asserts:
        print(static_assert)
    exit(0)

print(f'\n    cti::Dlopen::Handle {handle_name};\n')

for function_member in function_members:
    print(f'    {function_member};')

print(f'\n    {struct_name}(std::string const& {path_name});\n}};')

print(f'\n{namespace_name}::{struct_name}::{struct_name}(std::string const& {path_name})')
print(f'    : {handle_name}{{{path_name}}}')

for function_initializer in function_initializers:
  print(f'    , {function_initializer}')

print('{}')
