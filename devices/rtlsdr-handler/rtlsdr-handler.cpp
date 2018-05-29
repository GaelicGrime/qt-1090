#
/*
 *    Copyright (C) 2018
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Programming
 *
 *    This file is part of the dump1090 program
 *
 *    dump1090 is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    dump1090 is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with dump1090; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include	<QThread>
#include	"rtlsdr-handler.h"
#include	"adsb-constants.h"
#include        "rtl-dongleselect.h"

#ifdef  __MINGW32__
#define GETPROCADDRESS  GetProcAddress
#else
#define GETPROCADDRESS  dlsym
#endif

#define		READLEN_DEFAULT	(2 * 8192)

static
void rtlsdrCallback (unsigned char *buf, uint32_t len, void *ctx) {
rtlsdrHandler *theStick = static_cast<rtlsdrHandler *>(ctx);
uint32_t i;
int16_t	lbuf [READLEN_DEFAULT / 2];
//	"len" denotes incoming bytes here
	if (len > READLEN_DEFAULT)
	   len = READLEN_DEFAULT;

	for (i = 0; i < len / 2; i ++) {
	   int16_t re = (int16_t)((int8_t)(buf [2 * i] - 128));
	   int16_t im = (int16_t)((int8_t)(buf [2 * i + 1] - 128));
	   lbuf [i] = (re < 0 ? -re : re) + (im < 0 ? -im : im);
	}

        theStick -> _I_Buffer -> putDataIntoBuffer (lbuf, len / 2);
        if (theStick -> _I_Buffer -> GetRingBufferReadAvailable () > 256000) {
	   theStick -> signalData ();
	}
}

class   dll_driver : public QThread {
private:
        rtlsdrHandler   *theStick;
public:

        dll_driver (rtlsdrHandler *d) {
        theStick        = d;
        start ();
        }

        ~dll_driver (void) {
        }

private:
virtual void    run (void) {
	fprintf (stderr, "here we go\n");
        theStick -> rtlsdr_read_async (theStick -> theDevice,
                                       &rtlsdrCallback,
                                       (void *)theStick,
                                       0,
                                       READLEN_DEFAULT);
        }
};


	rtlsdrHandler::rtlsdrHandler (QSettings *s,
	                              int freq) {
int j;
int	deviceCount;
int	deviceIndex;

	this	-> freq		= freq;
	workerHandle		= NULL;
	rtlsdrSettings		= s;
	this	-> myFrame	= new QFrame (NULL);
	setupUi (this -> myFrame);
	myFrame	-> show ();

#ifdef  __MINGW32__
        const char *libraryString = "rtlsdr.dll";
        Handle          = LoadLibrary ((wchar_t *)L"rtlsdr.dll");
#else
        const char *libraryString = "librtlsdr.so";
        Handle          = dlopen ("librtlsdr.so", RTLD_NOW);
#endif

        if (Handle == NULL) {
           fprintf (stderr, "failed to open %s\n", libraryString);
           delete myFrame;
           throw (20);
        }

      libraryLoaded   = true;
        if (!load_rtlFunctions ()) {
#ifdef __MINGW32__
           FreeLibrary (Handle);
#else
           dlclose (Handle);
#endif
           delete myFrame;
           throw (21);
        }
//
//      Ok, from here we have the library functions accessible

	deviceCount = this -> rtlsdr_get_device_count ();
	if (deviceCount == 0) {
	   fprintf(stderr, "No supported RTLSDR devices found.\n");
#ifdef __MINGW32__
           FreeLibrary (Handle);
#else
           dlclose (Handle);
#endif
           delete myFrame;
           throw (22);
        }

	deviceIndex = 0;        // default
        if (deviceCount > 1) {
           rtl_dongleSelect dongleSelector;
           for (deviceIndex = 0; deviceIndex < deviceCount; deviceIndex ++) {
              dongleSelector.
                   addtoDongleList (this -> rtlsdr_get_device_name (deviceIndex));
           }
           deviceIndex = dongleSelector. QDialog::exec ();
        }
//

	if (this -> rtlsdr_open (&theDevice, deviceIndex) < 0) {
	   fprintf (stderr, "Error opening the RTLSDR device: %s\n",
	                                               strerror (errno));
#ifdef __MINGW32__
           FreeLibrary (Handle);
#else
           dlclose (Handle);
#endif
           delete myFrame;
           throw (23);
	}


	gainsCount = this -> rtlsdr_get_tuner_gains (theDevice, NULL);
        fprintf (stderr, "Supported gain values (%d): ", gainsCount);
        gains           = new int [gainsCount];
        gainsCount = this -> rtlsdr_get_tuner_gains (theDevice, gains);
        for (int i = gainsCount; i > 0; i--) {
           fprintf(stderr, "%.1f ", gains [i - 1] / 10.0);
           combo_gain -> addItem (QString::number (gains [i - 1]));
        }
        fprintf(stderr, "\n");
	delete[] gains;

	this -> rtlsdr_set_tuner_gain_mode (theDevice, 1);

	this -> rtlsdr_set_center_freq (theDevice, freq);
	this -> rtlsdr_set_sample_rate (theDevice, 2400000);
	this -> rtlsdr_reset_buffer    (theDevice);
	_I_Buffer		= new RingBuffer<int16_t> (32 * 32768);
//
//	See what the saved values are and restore the GUI settings
        rtlsdrSettings  -> beginGroup ("rtlsdrSettings");
        QString temp =
	   rtlsdrSettings -> value ("externalGain", "100"). toString ();
        int k       = combo_gain -> findText (temp);
        if (k != -1) {
           combo_gain   -> setCurrentIndex (k);
        }

        temp    = rtlsdrSettings -> value ("autogain",
                                              "autogain_on"). toString ();
        k       = combo_autogain -> findText (temp);
        if (k != -1)
           combo_autogain       -> setCurrentIndex (k);

        ppm_correction  -> setValue (rtlsdrSettings -> value ("ppm_correction", 0). toInt ());
	rtlsdrSettings  -> endGroup ();

//      all sliders/values are set to previous values, now do the settings
//      based on these slider values
        this -> rtlsdr_set_tuner_gain_mode (theDevice,
                           combo_autogain -> currentText () == "autogain_on");
        if (combo_autogain -> currentText () == "autogain_on")
           this -> rtlsdr_set_agc_mode (theDevice, 1);
        else
           this -> rtlsdr_set_agc_mode (theDevice, 0);
        this -> rtlsdr_set_tuner_gain   (theDevice,
	                           combo_gain -> currentText (). toInt ());
        set_ppmCorrection       (ppm_correction -> value ());

//      and attach the buttons/sliders to the actions
        connect (combo_gain, SIGNAL (activated (const QString &)),
                 this, SLOT (set_ExternalGain (const QString &)));
        connect (combo_autogain, SIGNAL (activated (const QString &)),
                 this, SLOT (set_autogain (const QString &)));
        connect (ppm_correction, SIGNAL (valueChanged (int)),
                 this, SLOT (set_ppmCorrection  (int)));
}

	rtlsdrHandler::~rtlsdrHandler	(void) {
	if (Handle == NULL) {
	   delete myFrame;
	   return;	
	}

	stopDevice ();
        this -> rtlsdr_close (theDevice);
#ifdef __MINGW32__
	FreeLibrary (Handle);
#else
	dlclose (Handle);
#endif
        rtlsdrSettings  -> beginGroup ("rtlsdrSettings");
        rtlsdrSettings  -> setValue ("externalGain",
                                              combo_gain -> currentText ());
        rtlsdrSettings  -> setValue ("autogain",
                                              combo_autogain -> currentText ());
        rtlsdrSettings  -> setValue ("ppm_correction",
                                              ppm_correction -> value ());
        rtlsdrSettings  -> sync ();
        rtlsdrSettings  -> endGroup ();

	delete	myFrame;
}

void	rtlsdrHandler::startDevice (void) {
	if (workerHandle != NULL)
	   return;

        _I_Buffer       -> FlushRingBuffer ();
        int r = this -> rtlsdr_reset_buffer (theDevice);
        if (r < 0)
           return;

        this -> rtlsdr_set_center_freq (theDevice, freq);
        workerHandle    = new dll_driver (this);
        this -> rtlsdr_set_agc_mode (theDevice,
                combo_autogain -> currentText () == "autogain_on" ? 1 : 0);
        this -> rtlsdr_set_tuner_gain (theDevice,
	                      combo_gain -> currentText (). toInt ());
}

void	rtlsdrHandler::stopDevice	(void) {
	if (workerHandle == NULL)
           return;
	this -> rtlsdr_cancel_async (theDevice);
	while (!workerHandle -> isFinished ())
	   usleep (1000);
	delete    workerHandle;
	workerHandle = NULL;
	myFrame	-> hide ();
}

int	rtlsdrHandler::getSamples (int16_t *buffer, int amount) {
	_I_Buffer      -> getDataFromBuffer (buffer, amount);
	return amount;
}

int	rtlsdrHandler::Samples (void) {
	return _I_Buffer -> GetRingBufferReadAvailable ();
}

void	rtlsdrHandler::signalData	(void) {
	emit dataAvailable ();
}
//
//      when selecting  the gain from a table, use the table value
void    rtlsdrHandler::set_ExternalGain (const QString &gain) {
	this -> rtlsdr_set_tuner_gain (theDevice, gain. toInt ());
}
//
void    rtlsdrHandler::set_autogain     (const QString &autogain) {
	this -> rtlsdr_set_agc_mode (theDevice,
	                             autogain == "autogain_off" ? 0 : 1);
        this -> rtlsdr_set_tuner_gain (theDevice,
	                       combo_gain -> currentText (). toInt ());
}
//
void    rtlsdrHandler::set_ppmCorrection        (int32_t ppm) {
        this -> rtlsdr_set_freq_correction (theDevice, ppm);
}

bool	rtlsdrHandler::load_rtlFunctions (void) {
//
//	link the required procedures
	rtlsdr_open	= (pfnrtlsdr_open)
	                       GETPROCADDRESS (Handle, "rtlsdr_open");
	if (rtlsdr_open == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_open\n");
	   return false;
	}
	rtlsdr_close	= (pfnrtlsdr_close)
	                     GETPROCADDRESS (Handle, "rtlsdr_close");
	if (rtlsdr_close == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_close\n");
	   return false;
	}

	rtlsdr_set_sample_rate =
	    (pfnrtlsdr_set_sample_rate)GETPROCADDRESS (Handle, "rtlsdr_set_sample_rate");
	if (rtlsdr_set_sample_rate == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_sample_rate\n");
	   return false;
	}

	rtlsdr_get_sample_rate	=
	    (pfnrtlsdr_get_sample_rate)GETPROCADDRESS (Handle, "rtlsdr_get_sample_rate");
	if (rtlsdr_get_sample_rate == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_sample_rate\n");
	   return false;
	}

	rtlsdr_get_tuner_gains		= (pfnrtlsdr_get_tuner_gains)
	                     GETPROCADDRESS (Handle, "rtlsdr_get_tuner_gains");
	if (rtlsdr_get_tuner_gains == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_tuner_gains\n");
	   return false;
	}


	rtlsdr_set_tuner_gain_mode	= (pfnrtlsdr_set_tuner_gain_mode)
	                     GETPROCADDRESS (Handle, "rtlsdr_set_tuner_gain_mode");
	if (rtlsdr_set_tuner_gain_mode == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_tuner_gain_mode\n");
	   return false;
	}

	rtlsdr_set_agc_mode	= (pfnrtlsdr_set_agc_mode)
	                     GETPROCADDRESS (Handle, "rtlsdr_set_agc_mode");
	if (rtlsdr_set_agc_mode == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_agc_mode\n");
	   return false;
	}

	rtlsdr_set_tuner_gain	= (pfnrtlsdr_set_tuner_gain)
	                     GETPROCADDRESS (Handle, "rtlsdr_set_tuner_gain");
	if (rtlsdr_set_tuner_gain == NULL) {
	   fprintf (stderr, "Cound not find rtlsdr_set_tuner_gain\n");
	   return false;
	}

	rtlsdr_get_tuner_gain	= (pfnrtlsdr_get_tuner_gain)
	                     GETPROCADDRESS (Handle, "rtlsdr_get_tuner_gain");
	if (rtlsdr_get_tuner_gain == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_tuner_gain\n");
	   return false;
	}
	rtlsdr_set_center_freq	= (pfnrtlsdr_set_center_freq)
	                     GETPROCADDRESS (Handle, "rtlsdr_set_center_freq");
	if (rtlsdr_set_center_freq == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_center_freq\n");
	   return false;
	}

	rtlsdr_get_center_freq	= (pfnrtlsdr_get_center_freq)
	                     GETPROCADDRESS (Handle, "rtlsdr_get_center_freq");
	if (rtlsdr_get_center_freq == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_center_freq\n");
	   return false;
	}

	rtlsdr_reset_buffer	= (pfnrtlsdr_reset_buffer)
	                     GETPROCADDRESS (Handle, "rtlsdr_reset_buffer");
	if (rtlsdr_reset_buffer == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_reset_buffer\n");
	   return false;
	}

	rtlsdr_read_async	= (pfnrtlsdr_read_async)
	                     GETPROCADDRESS (Handle, "rtlsdr_read_async");
	if (rtlsdr_read_async == NULL) {
	   fprintf (stderr, "Cound not find rtlsdr_read_async\n");
	   return false;
	}

	rtlsdr_get_device_count	= (pfnrtlsdr_get_device_count)
	                     GETPROCADDRESS (Handle, "rtlsdr_get_device_count");
	if (rtlsdr_get_device_count == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_device_count\n");
	   return false;
	}

	rtlsdr_cancel_async	= (pfnrtlsdr_cancel_async)
	                     GETPROCADDRESS (Handle, "rtlsdr_cancel_async");
	if (rtlsdr_cancel_async == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_cancel_async\n");
	   return false;
	}

	rtlsdr_set_direct_sampling = (pfnrtlsdr_set_direct_sampling)
	                  GETPROCADDRESS (Handle, "rtlsdr_set_direct_sampling");
	if (rtlsdr_set_direct_sampling == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_direct_sampling\n");
	   return false;
	}

	rtlsdr_set_freq_correction = (pfnrtlsdr_set_freq_correction)
	                  GETPROCADDRESS (Handle, "rtlsdr_set_freq_correction");
	if (rtlsdr_set_freq_correction == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_freq_correction\n");
	   return false;
	}
	
	rtlsdr_get_device_name = (pfnrtlsdr_get_device_name)
	                  GETPROCADDRESS (Handle, "rtlsdr_get_device_name");
	if (rtlsdr_get_device_name == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_device_name\n");
	   return false;
	}

	fprintf (stderr, "OK, functions seem to be loaded\n");
	return true;
}
