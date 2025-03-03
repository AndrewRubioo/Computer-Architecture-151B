// Copyright 2024 blaise
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include <assert.h>
#include <util.h>
#include "types.h"
#include "core.h"
#include "debug.h"

using namespace tinyrv;

///////////////////////////////////////////////////////////////////////////////

GShare::GShare(uint32_t BTB_size, uint32_t BHR_size)
  : BTB_(BTB_size, BTB_entry_t{false, 0x0, 0x0})
  , PHT_((1 << BHR_size), 0x0) //size 256 all entries 0
  , BHR_(0x0)
  , BTB_shift_(log2ceil(BTB_size))
  , BTB_mask_(BTB_size-1)
  , BHR_mask_((1 << BHR_size)-1) {
  //--
}

GShare::~GShare() {
  //--
}

uint32_t GShare::predict(uint32_t PC) {
  uint32_t next_PC = PC + 4;
  bool predict_taken = false;

  // TODO:
  uint32_t PHT_index = ((PC >> 2) ^ BHR_) & BHR_mask_; //index for PHT, PC shifted by 2
  uint8_t btb_index = (PC >> 2) & BTB_mask_;
  uint32_t tag = (PC >> 2) >> BTB_shift_; // tag shift by 22 then shift by BTB

  if (PHT_[PHT_index] >= 2)
  {
    predict_taken = true;
  }

  if (predict_taken){
    if(BTB_[btb_index].valid && BTB_[btb_index].tag == tag){
      next_PC = BTB_[btb_index].target_PC;
    //   std::cout << "[PREDICT] BTB Hit! Next PC: 0x" << std::hex << next_PC << std::endl;
    //   } 
    // else {
    //   std::cout << "[PREDICT] BTB Miss!\n";
    }
  }

//end my code
  DT(3, "*** GShare: predict PC=0x" << std::hex << PC << std::dec
        << ", next_PC=0x" << std::hex << next_PC << std::dec
        << ", predict_taken=" << predict_taken);
  return next_PC;
}

void GShare::update(uint32_t PC, uint32_t next_PC, bool taken) {
    // TODO:
  uint32_t index = ((PC >> 2) ^ BHR_) & BHR_mask_; //index for PHT, PC shifted by 2
  uint32_t tag = (PC >> 2) >> BTB_shift_; // tag shift by 2 then shift by BTB

  if (taken) {
      if (PHT_[index] < 3) {
          PHT_[index]++;
      }
  } else {
      if (PHT_[index] > 0) {
          PHT_[index]--;
      }
  }
  BHR_ = ((BHR_ << 1) | taken) & BHR_mask_; //update BHR

  //update BTB
  if (taken){
  uint32_t btb_index = (PC >> 2) & BTB_mask_;
  BTB_[btb_index].valid = true;
  BTB_[btb_index].target_PC = next_PC;
  BTB_[btb_index].tag = tag;
}
//end my code
  DT(3, "*** GShare: update PC=0x" << std::hex << PC << std::dec
        << ", next_PC=0x" << std::hex << next_PC << std::dec
        << ", taken=" << taken);

}

///////////////////////////////////////////////////////////////////////////////

GSharePlus::GSharePlus(uint32_t BTB_size, uint32_t BHR_size)
  : BTB_(BTB_size, BTB_entry_t{false, 0x0, 0x0})
  , PHT_((1 << BHR_size), 0x0) // Global PHT
  , LPHT_((1 << 8), 0x0) // Local PHT (8-bit history)
  , LHT_(BTB_size, 0x0) // Local History Table
  , MetaPredictor_(BTB_size, 0x4) // Meta Predictor (3-bit counters, initialized to 4)
  , BHR_(0x0)
  , BTB_shift_(log2ceil(BTB_size))
  , BTB_mask_(BTB_size > 0 ? BTB_size - 1 : 0) // Ensure BTB_mask_ is valid
  , BHR_mask_((1 << BHR_size) - 1)
  , LPHT_mask_((1 << 8) - 1) // 8-bit local PHT
  , LHT_mask_((1 << 8) - 1) { // 8-bit local history
  // Ensure BTB_size and BHR_size are valid
  // if (BTB_size == 0 || BHR_size == 0) {
  //   std::cerr << "Error: BTB_size and BHR_size must be greater than 0" << std::endl;
  //   std::abort();
  // }
}

GSharePlus::~GSharePlus() {
  //--
}

uint32_t GSharePlus::predict(uint32_t PC) {
  uint32_t next_PC = PC + 4;
  bool predict_taken = false;

  // Calculate indices
  uint32_t PHT_index = ((PC >> 2) ^ BHR_) & BHR_mask_; // Index for PHT
  uint32_t btb_index = (PC >> 2) & BTB_mask_; // Index for BTB
  uint32_t lht_index = btb_index; // LHT = BTB index
  uint32_t lpht_index = LHT_[lht_index] & LPHT_mask_; // Index for LPHT

  // Get predictions from GShare and Local History predictors
  bool gshare_pred = (PHT_[PHT_index] >= 2); // GShare prediction
  bool local_pred = (LPHT_[lpht_index] >= 2); // Local History prediction

  // Use Meta Predictor to choose between GShare and Local History
  bool use_gshare = (MetaPredictor_[btb_index] >= 4); // 3-bit counter
    //is use_gshare true? then gshare predict. otherwise local history
  predict_taken = use_gshare ? gshare_pred : local_pred; 

  // If the branch is predicted taken, look up the target in the BTB
  if (predict_taken) {
    if (BTB_[btb_index].valid && BTB_[btb_index].tag == (PC >> BTB_shift_)) {
      next_PC = BTB_[btb_index].target_PC;
    }
  }

  DT(3, "*** GShare+: predict PC=0x" << std::hex << PC << std::dec
        << ", next_PC=0x" << std::hex << next_PC << std::dec
        << ", predict_taken=" << predict_taken);
  return next_PC;
}

void GSharePlus::update(uint32_t PC, uint32_t next_PC, bool taken) {
  // Calculate indices
  uint32_t PHT_index = ((PC >> 2) ^ BHR_) & BHR_mask_; // Index for PHT
  uint32_t btb_index = (PC >> 2) & BTB_mask_; //Index for BTB
  uint32_t lht_index = btb_index; //LHT = BTB index
  uint32_t lpht_index = LHT_[lht_index] & LPHT_mask_; // Index for LPHT

  // Update GShare predictor
  if (taken) {
      if (PHT_[PHT_index] < 3) {
          PHT_[PHT_index]++;
      }
  } else {
      if (PHT_[PHT_index] > 0) {
          PHT_[PHT_index]--;
      }
  }

  // Update Local History predictor
  if (taken) {
      if (LPHT_[lpht_index] < 3) {
          LPHT_[lpht_index]++;
      }
  } else {
      if (LPHT_[lpht_index] > 0) {
          LPHT_[lpht_index]--;
      }
  }
  LHT_[lht_index] = ((LHT_[lht_index] << 1) | taken) & LHT_mask_; // Update LHT

  // Update Meta Predictor
  bool gshare_correct = (PHT_[PHT_index] >= 2) == taken;
  bool local_correct = (LPHT_[lpht_index] >= 2) == taken;

if (gshare_correct != local_correct) {
  if (gshare_correct) {
    if (MetaPredictor_[btb_index] < 7) {
      MetaPredictor_[btb_index]++;
    }
  } else {
    if (MetaPredictor_[btb_index] > 0) {
      MetaPredictor_[btb_index]--;
    }
  }
}
  // Update BTB if the branch was taken
  if (taken) {
    BTB_[btb_index] = {true, next_PC, PC >> BTB_shift_};
  }

  DT(3, "*** GShare+: update PC=0x" << std::hex << PC << std::dec
        << ", next_PC=0x" << std::hex << next_PC << std::dec
        << ", taken=" << taken);
}
