/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// for S-JIS encoding, obeserve characteristic:
// 1, kana character (or hankaku?) often have hight frequency of appereance
// 2, kana character often exist in group
// 3, certain combination of kana is never used in japanese language

#ifndef nsSJISProber_h__
#define nsSJISProber_h__

#include "nsCharSetProber.h"
#include "nsCodingStateMachine.h"
#include "JpCntx.h"
#include "CharDistribution.h"


class nsSJISProber: public nsCharSetProber {
public:
  nsSJISProber(bool aIsPreferredLanguage)
    :mIsPreferredLanguage(aIsPreferredLanguage)
  {mCodingSM = new nsCodingStateMachine(&SJISSMModel);
    Reset();}
  ~nsSJISProber(void) override{delete mCodingSM;}
  nsProbingState HandleData(const char* aBuf, uint32_t aLen) override;
  const char* GetCharSetName() override {return "Shift_JIS";}
  nsProbingState GetState(void) override {return mState;}
  void      Reset(void) override;
  float     GetConfidence(void) override;

protected:
  nsCodingStateMachine* mCodingSM;
  nsProbingState mState;

  SJISContextAnalysis mContextAnalyser;
  SJISDistributionAnalysis mDistributionAnalyser;

  char mLastChar[2];
  bool mIsPreferredLanguage;

};


#endif /* nsSJISProber_h__ */

