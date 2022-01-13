#!/usr/bin/env ruby
#
# common sub function.
#
#  Copyright (C) 2015-2020 Kyushu Institute of Technology.
#  Copyright (C) 2015-2020 Shimane IT Open-Innovation Center.
#
#  This file is distributed under BSD 3-Clause License.
#
#

RENAME_CHAR = {
  "!"=>"NOT",   # or "E"
  "%"=>"MOD",
  "&"=>"AND",
  "*"=>"MUL",
  "+"=>"PLUS",
  "-"=>"MINUS",
  "/"=>"DIV",
  "<"=>"LT",
  "="=>"EQ",
  ">"=>"GT",
  "?"=>"Q",
  "@"=>"AT",
  "["=>"BL",
  "]"=>"BR",
  "^"=>"XOR",
  "|"=>"OR",
  "~"=>"NEG"
}

##
# rename for symbol
#
def rename_for_symbol(s)
  return "#{$1}_E"  if /^(.+)!$/ =~ s

  r = ""
  s.each_char {|ch|
    if !RENAME_CHAR[ch]
      r << ch
    else
      r << "_"  if !r.empty?
      r << RENAME_CHAR[ch]
    end
  }
  return r
end


##
# strip double quot
#
def strip_double_quot( s )
  ret = s.dup
  ret.slice!(0)  if ret[0] == '"'
  ret.chop! if ret[-1] == '"'

  return ret
end


#
# find and get source code.
#
def get_method_table_source( file )
  ret = nil

  # find magic comment
  while txt = file.gets
    if /\/\*\s+MRBC_AUTOGEN_METHOD_TABLE/ =~ txt
      ret = ""
      break
    end
  end
  return nil  if !ret

  while txt = file.gets
    # skip comment and empty line.
    txt = txt.split("//", 2)[0].strip
    next if txt.empty?

    break  if txt.start_with?("*/")
    ret << txt << "\n"
  end

  return ret
end


##
# parser
#
# (note)
#  create hash, below.
# {
#   class: "Range",
#   file: "c_range_method_table.h",
#   super: "mrbc_class_object",
#   methods: [
#     { name: "first", func: "c_range_first", if_exp: "" }
#       ...
#   ]
# }
#
def parse_source_string( src )
  flag_error = false
  ret = { methods:[] }
  if_exp = []
  src.each_line {|txt|
    # e.g. "#if ..."
    if /^#\s*if/ =~ txt
      if_exp << txt
      next
    end
    if /^#\s*endif/ =~ txt
      if_exp.pop
      next
    end

    # e.g. CLASS, METHOD and etc.
    if /^([A-Z]+)\s*\((.*)\)/ =~ txt
      key = $1
      args = $2.split(",").map {|s| s.strip }
      flag_arg_ok = true
      case key
      when "CLASS", "FILE", "SUPER"
        if args.size == 1
          ret[key.downcase.to_sym] = strip_double_quot(args[0])
        else
          flag_arg_ok = false
        end

      when "METHOD"
        if args.size == 2
          ret[:methods] << { name: strip_double_quot(args[0]),
                             func: strip_double_quot(args[1]) }
          ret[:methods].last[:if_exp] = if_exp.dup  if !if_exp.empty?
        else
          flag_arg_ok = false
        end

      else
        puts "Error: Invalid keyword. '#{key}'"
      end

      if !flag_arg_ok
        puts "Error: argument error. #{txt}"
      end
      next
    end

    puts "Error: #{txt}"
    flag_error = true
  }

  return flag_error ? nil : ret
end


##
# error check
#
def check_error( param )
  flag_error = false

  if !param[:class]
    puts "Error: 'CLASS' parameter required"
    flag_error = true
  end
  if !param[:file]
    puts "Error: 'FILE' parameter required"
    flag_error = true
  end

  return !flag_error
end
