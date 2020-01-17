/*! @file
  @brief
  exception classes

  <pre>
  Copyright (C) 2015-2019 Kyushu Institute of Technology.
  Copyright (C) 2015-2019 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  Fetch mruby VM bytecodes, decode and execute.

  </pre>
*/

#include "vm_config.h"
#include <stddef.h>
#include <string.h>
#include "vm.h"
#include "error.h"
#include "static.h"

void mrbc_init_class_exception(struct VM *vm)
{
  mrbc_class_exception = mrbc_define_class(vm, "Exception", mrbc_class_object);

  mrbc_class_standarderror = mrbc_define_class(vm, "StandardError", mrbc_class_exception);
  mrbc_class_runtimeerror = mrbc_define_class(vm, "RuntimeError", mrbc_class_standarderror);
  mrbc_class_zerodivisionerror = mrbc_define_class(vm, "ZeroDivisionError", mrbc_class_standarderror);
  mrbc_class_argumenterror = mrbc_define_class(vm, "ArgumentError", mrbc_class_standarderror);
  mrbc_class_indexerror = mrbc_define_class(vm, "IndexError", mrbc_class_standarderror);
  mrbc_class_typeerror = mrbc_define_class(vm, "TypeError", mrbc_class_standarderror);
}





