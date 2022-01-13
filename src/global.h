/*! @file
  @brief
  Constant and global variables.

  <pre>
  Copyright (C) 2015-2020 Kyushu Institute of Technology.
  Copyright (C) 2015-2020 Shimane IT Open-innovation Center.

  This file is distributed under BSD 3-Clause License.

  </pre>
*/

#ifndef MRBC_SRC_GLOBAL_H_
#define MRBC_SRC_GLOBAL_H_

#include "value.h"
#include "class.h"

#ifdef __cplusplus
extern "C" {
#endif

void mrbc_init_global(void);
int mrbc_set_const(mrbc_sym sym_id, mrbc_value *v);
int mrbc_set_class_const(const mrbc_class *cls, mrbc_sym sym_id, mrbc_value *v);
mrbc_value *mrbc_get_const(mrbc_sym sym_id);
mrbc_value *mrbc_get_class_const(const mrbc_class *cls, mrbc_sym sym_id);
int mrbc_set_global(mrbc_sym sym_id, mrbc_value *v);
mrbc_value *mrbc_get_global(mrbc_sym sym_id);
void mrbc_global_clear_vm_id(void);
void mrbc_global_debug_dump(void);


#ifdef __cplusplus
}
#endif
#endif
