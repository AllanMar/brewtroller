#ifdef RGBIO8_ENABLE
#define ELEMSOFTSW_RGBIOIN 7
#define ELEMSOFTSW_HLTIN 0
#define ELEMSOFTSW_BKIN 1

#define ELEMSOFTSW_HLTENOUT 8
#define ELEMSOFTSW_BKENOUT 10
#define ELEMSOFTSW_DUALENOUT 9
#define ELEMSOFTSW_HLTOUT 12
#define ELEMSOFTSW_BKOUT 11


  unsigned long lastRGBIO8 = 0;
  
  void RGBIO8_Update() {
    if (millis() > (lastRGBIO8 + RGBIO8_INTERVAL)) {
      for (int i = 0; i < RGBIO8_MAX_BOARDS; i++) {
        if (rgbio[i])
          rgbio[i]->update();
      }

	  unsigned long disableMask = 0;
	  unsigned long profileMask = 0;
	  if (rgbio[0]->isAuto(ELEMSOFTSW_HLTIN) || rgbio[0]->isManual(ELEMSOFTSW_HLTIN)) //HLT set to AUTO/ON
		  profileMask |= (1 << ELEMSOFTSW_HLTENOUT);

	  if (rgbio[0]->isAuto(ELEMSOFTSW_BKIN) || rgbio[0]->isManual(ELEMSOFTSW_BKIN)) //BK set to AUTO/ON
		  profileMask |= (1 << ELEMSOFTSW_BKENOUT);

	  if (rgbio[0]->isAuto(ELEMSOFTSW_RGBIOIN)) { //HLT Selected
		  disableMask |= (1 << ELEMSOFTSW_BKENOUT)|(1 << ELEMSOFTSW_BKOUT);
		  profileMask |= (1 << ELEMSOFTSW_DUALENOUT);
	  } else if (rgbio[0]->isManual(ELEMSOFTSW_RGBIOIN)) { //BK Selected
		  disableMask |= (1 << ELEMSOFTSW_HLTENOUT)|(1 << ELEMSOFTSW_HLTOUT);
		  profileMask |= (1 << ELEMSOFTSW_DUALENOUT);
	  } 
	  outputs->setProfileMask(OUTPUTPROFILE_ELEMSOFTSW, profileMask);
	  outputs->setOutputEnableMask(OUTPUTENABLE_ELEMSOFTSW, ~disableMask);
	  outputs->setProfileState(OUTPUTPROFILE_ELEMSOFTSW, 1);

      outputs->setProfileState(OUTPUTPROFILE_RGBIO, outputs->getProfileMask(OUTPUTPROFILE_RGBIO) ? 1 : 0);
      lastRGBIO8 = millis();
    }
  }
#endif
