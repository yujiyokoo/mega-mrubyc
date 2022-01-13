/*! @file
  @brief
  exception

  <pre>
  Copyright (C) 2015-2021 Kyushu Institute of Technology.
  Copyright (C) 2015-2021 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.


  </pre>
*/

#ifndef MRBC_SRC_ERROR_H_
#define MRBC_SRC_ERROR_H_

/***** Feature test switches ************************************************/
/***** System headers *******************************************************/
/***** Local headers ********************************************************/
#include "value.h"
#include "c_string.h"

#ifdef __cplusplus
extern "C" {
#endif
/***** Constat values *******************************************************/
/***** Macros ***************************************************************/
#define mrbc_israised(vm) (mrbc_type((vm)->exception) == MRBC_TT_EXCEPTION)


/***** Typedefs *************************************************************/
//================================================================
/*!@brief
  Define Exception handle.
*/
typedef struct RException {
  MRBC_OBJECT_HEADER;

  struct RClass *cls;			//!< exception class.
  MRBC_STRING_SIZE_T message_size;	//!< message length.
  uint8_t *message;			//!< pointer to allocated buffer.

} mrbc_exception;


/***** Global variables *****************************************************/
mrbc_value mrbc_exception_new(struct VM *vm, struct RClass *exc_cls, const void *message, int len);
void mrbc_exception_delete(mrbc_value *value);
void mrbc_exception_set_message(struct VM *vm, mrbc_value *value, const void *message, int len);
void mrbc_raise(struct VM *vm, struct RClass *exc_cls, const char *msg);


/***** Function prototypes **************************************************/
/***** Inline functions *****************************************************/


#ifdef __cplusplus
}
#endif
#endif
