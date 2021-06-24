#!/usr/bin/python3

# Copyright 2020 Hewlett Packard Enterprise Development LP.
#
#     Redistribution and use in source and binary forms, with or
#     without modification, are permitted provided that the following
#     conditions are met:
#
#      - Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      - Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials
#        provided with the distribution.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import sys

def snake_to_camel(str):
  return ''.join(word.title() for word in str.split('_'))

namespace_name = ""
struct_name = ""
path_name = ""
handle_name = ""

function_types = []
function_members = []
function_initializers = []

with open(sys.argv[1]) as file:
 for line in file:
  line = line.rstrip('\n')
  if not namespace_name:
    namespace_name = line
  elif not struct_name:
    struct_name = line
  elif not path_name:
    path_name = line
  elif not handle_name:
    handle_name = line
  elif line:
    (function_name, function_type) = line.split(maxsplit=1)
    type_name = snake_to_camel(function_name)
    function_types.append("using %s = %s" % (type_name, function_type))
    function_members.append("std::function<%s> %s" % (type_name, function_name))
    function_initializers.append("%s{%s.load<%s>(\"%s\")}" % (function_name, handle_name, type_name, function_name))

print('#pragma once')

print(f'struct {namespace_name}::{struct_name}\n{{')

for function_type in function_types:
    print(f'    {function_type};')

print(f'\n    cti::Dlopen::Handle {handle_name};\n')

for function_member in function_members:
    print(f'    {function_member};')

print(f'\n    {struct_name}(std::string const& {path_name});\n}};')

print(f'\n{namespace_name}::{struct_name}::{struct_name}(std::string const& {path_name})')
print(f'    : {handle_name}{{{path_name}}}')

for function_initializer in function_initializers:
  print(f'    , {function_initializer}')

print('{}')
