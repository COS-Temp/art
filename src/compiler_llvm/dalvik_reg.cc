/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dalvik_reg.h"

#include "ir_builder.h"
#include "method_compiler.h"

using namespace art;
using namespace art::compiler_llvm;


//----------------------------------------------------------------------------
// Dalvik Register
//----------------------------------------------------------------------------

DalvikReg::DalvikReg(MethodCompiler& method_compiler, const std::string& name, llvm::Value* vreg)
: method_compiler_(&method_compiler), irb_(method_compiler.GetIRBuilder()),
  reg_name_(name), reg_32_(NULL), reg_64_(NULL), reg_obj_(NULL), vreg_(vreg) {
}


DalvikReg::~DalvikReg() {
}


llvm::Type* DalvikReg::GetRegCategoryEquivSizeTy(IRBuilder& irb, RegCategory reg_cat) {
  switch (reg_cat) {
  case kRegCat1nr:  return irb.getJIntTy();
  case kRegCat2:    return irb.getJLongTy();
  case kRegObject:  return irb.getJObjectTy();
  default:
    LOG(FATAL) << "Unknown register category: " << reg_cat;
    return NULL;
  }
}


char DalvikReg::GetRegCategoryNamePrefix(RegCategory reg_cat) {
  switch (reg_cat) {
  case kRegCat1nr:  return 'r';
  case kRegCat2:    return 'w';
  case kRegObject:  return 'p';
  default:
    LOG(FATAL) << "Unknown register category: " << reg_cat;
    return '\0';
  }
}


inline llvm::Value* DalvikReg::RegCat1SExt(llvm::Value* value) {
  return irb_.CreateSExt(value, irb_.getJIntTy());
}


inline llvm::Value* DalvikReg::RegCat1ZExt(llvm::Value* value) {
  return irb_.CreateZExt(value, irb_.getJIntTy());
}


inline llvm::Value* DalvikReg::RegCat1Trunc(llvm::Value* value,
                                            llvm::Type* ty) {
  return irb_.CreateTrunc(value, ty);
}


llvm::Value* DalvikReg::GetValue(JType jty, JTypeSpace space) {
  DCHECK_NE(jty, kVoid) << "Dalvik register will never be void type";

  llvm::Value* value = NULL;
  switch (space) {
  case kReg:
  case kField:
    value = irb_.CreateLoad(GetAddr(jty), kTBAARegister);
    break;

  case kAccurate:
  case kArray:
    switch (jty) {
    case kVoid:
      LOG(FATAL) << "Dalvik register with void type has no value";
      return NULL;

    case kBoolean:
    case kChar:
    case kByte:
    case kShort:
      // NOTE: In array type space, boolean is truncated from i32 to i8, while
      // in accurate type space, boolean is truncated from i32 to i1.
      // For the other cases, array type space is equal to accurate type space.
      value = RegCat1Trunc(irb_.CreateLoad(GetAddr(jty), kTBAARegister),
                           irb_.getJType(jty, space));
      break;

    case kInt:
    case kLong:
    case kFloat:
    case kDouble:
    case kObject:
      value = irb_.CreateLoad(GetAddr(jty), kTBAARegister);
      break;

    default:
      LOG(FATAL) << "Unknown java type: " << jty;
      return NULL;
    }
    break;
  }

  if (jty == kFloat || jty == kDouble) {
    value = irb_.CreateBitCast(value, irb_.getJType(jty, space));
  }
  return value;
}


void DalvikReg::SetValue(JType jty, JTypeSpace space, llvm::Value* value) {
  DCHECK_NE(jty, kVoid) << "Dalvik register will never be void type";

  if (jty == kFloat || jty == kDouble) {
    value = irb_.CreateBitCast(value, irb_.getJType(jty, kReg));
  }

  switch (space) {
  case kReg:
  case kField:
    break;

  case kAccurate:
  case kArray:
    switch (jty) {
    case kVoid:
      LOG(FATAL) << "Dalvik register with void type has no value";
      break;

    case kBoolean:
    case kChar:
      // NOTE: In accurate type space, we have to zero extend boolean from
      // i1 to i32, and char from i16 to i32.  In array type space, we have
      // to zero extend boolean from i8 to i32, and char from i16 to i32.
      value = RegCat1ZExt(value);
      break;

    case kByte:
    case kShort:
      // NOTE: In accurate type space, we have to signed extend byte from
      // i8 to i32, and short from i16 to i32.  In array type space, we have
      // to sign extend byte from i8 to i32, and short from i16 to i32.
      value = RegCat1SExt(value);
      break;

    case kInt:
    case kLong:
    case kFloat:
    case kDouble:
    case kObject:
      break;

    default:
      LOG(FATAL) << "Unknown java type: " << jty;
    }
  }

  irb_.CreateStore(value, GetAddr(jty), kTBAARegister);
  if (vreg_ != NULL) {
    irb_.CreateStore(value,
                     irb_.CreateBitCast(vreg_, value->getType()->getPointerTo()),
                     kTBAAShadowFrame);
  }
}


llvm::Value* DalvikReg::GetAddr(JType jty) {
  switch (GetRegCategoryFromJType(jty)) {
  case kRegCat1nr:
    if (reg_32_ == NULL) {
      reg_32_ = method_compiler_->AllocDalvikReg(kRegCat1nr, reg_name_);
    }
    return reg_32_;

  case kRegCat2:
    if (reg_64_ == NULL) {
      reg_64_ = method_compiler_->AllocDalvikReg(kRegCat2, reg_name_);
    }
    return reg_64_;

  case kRegObject:
    if (reg_obj_ == NULL) {
      reg_obj_ = method_compiler_->AllocDalvikReg(kRegObject, reg_name_);
    }
    return reg_obj_;

  default:
    LOG(FATAL) << "Unexpected register category: "
               << GetRegCategoryFromJType(jty);
    return NULL;
  }
}
