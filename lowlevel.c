/**
 * NXT bootstrap interface; low-level USB functions.
 *
 * Copyright 2006 David Anderson <david.anderson@calixo.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#include "lowlevel.h"

/**
 * USB vendor and product ids for NXT.
 */
enum nxt_usb_ids {
  VENDOR_LEGO   = 0x0694,
  VENDOR_ATMEL  = 0x03EB,
  PRODUCT_NXT   = 0x0002,
  PRODUCT_SAMBA = 0x6124
};

/**
 * definitions of configuration, interface, and endpoints
 */
int CONFIG = 1;
int INTFC = 1;
unsigned char EP_OUT = 0x01;
unsigned char EP_IN = 0x82;

/**
 * Structure representing a NXT device.
 */
struct nxt_t {
  struct libusb_device *dev;
  struct libusb_device_handle *hdl;
  int is_in_reset_mode;
  int had_kernel_driver;
};

/**
 * Initialize a device and the underlying libusb library.
 */
nxt_error_t nxt_init(nxt_t **nxt) {
  int err = libusb_init(NULL);
  if (err!=0) return NXT_OTHER_ERROR;
  *nxt = calloc(1, sizeof(**nxt));

  return NXT_OK;
}

/**
 * Find the first NXT device on the USB bus.
 */
nxt_error_t nxt_find(nxt_t *nxt) {
  libusb_device** list;
  ssize_t numDev = libusb_get_device_list(NULL, &list);
  if (numDev<0) return NXT_OTHER_ERROR;
  
  nxt_error_t ret = NXT_NOT_PRESENT;
  
  struct libusb_device_descriptor* desc = 
    calloc(1,sizeof(struct libusb_device_descriptor));
  
  for (int i=0; i<numDev; i++) {
    libusb_device* dev = list[i];
    int err = libusb_get_device_descriptor(dev,desc);
    if (err!=0) {
      ret = NXT_OTHER_ERROR;
      break;
    }
    if (desc->idVendor == VENDOR_ATMEL &&
         desc->idProduct == PRODUCT_SAMBA) {
    	nxt->dev = dev;
    	nxt->is_in_reset_mode = 1;
    	ret = NXT_OK;
    	break;
    } else if (desc->idVendor == VENDOR_ATMEL &&
                desc->idProduct == PRODUCT_NXT) {
    	nxt->dev = dev;
    	ret = NXT_OK;
    	break;
    } else {
      libusb_unref_device(dev);
    }
  }
  
  free(desc);
  libusb_free_device_list(list,0);
  return ret;
}

/**
 * Open NXT device for use.
 */
nxt_error_t
nxt_open(nxt_t *nxt) {
  int BUFLEN = 2;
  char buf[BUFLEN];
	
	// open device and obtain handle
  int err = libusb_open(nxt->dev,&(nxt->hdl));
  if (err<0) return NXT_OTHER_ERROR;
  
	// detach kernel driver, if applicable
  int ret = NXT_OK;
	err = libusb_kernel_driver_active(nxt->hdl,INTFC);
	if (err==LIBUSB_ERROR_NO_DEVICE) { ret=NXT_NOT_PRESENT; goto errRet; }
	else if (err!=0 && err!=1 && err!=LIBUSB_ERROR_NOT_SUPPORTED) { 
	  ret=NXT_OTHER_ERROR;
	  goto errRet;
	} else if (err==1) {
	  err = libusb_detach_kernel_driver(nxt->hdl,INTFC);
	  if (err!=0) { ret=NXT_OTHER_ERROR; goto errRet; }
	  nxt->had_kernel_driver = 1;
  }
	
  // set configuration
  err = libusb_set_configuration(nxt->hdl, CONFIG);
  if (err<0) { ret = NXT_CONFIGURATION_ERROR; goto errRet; }
	
  // claim interface
  err = libusb_claim_interface(nxt->hdl, INTFC);
  if (err<0) { ret = NXT_IN_USE; goto errRet; }
	
  // check initial handshake
  nxt_send_str(nxt, "N#");
  nxt_recv_buf(nxt, buf, BUFLEN);
  if (memcmp(buf, "\n\r", BUFLEN) != 0) {
      libusb_release_interface(nxt->hdl, INTFC);
      ret = NXT_HANDSHAKE_FAILED;
      goto errRet;
  }
  
  return NXT_OK;
  
  // cleanup and error return
errRet:
  if (nxt->had_kernel_driver) libusb_attach_kernel_driver(nxt->hdl,INTFC);
  libusb_close(nxt->hdl);
  return ret;
}

/**
 * Close NXT device and deallocate.
 */
nxt_error_t
nxt_close(nxt_t *nxt) {
  libusb_release_interface(nxt->hdl, INTFC);
  if (nxt->had_kernel_driver) libusb_attach_kernel_driver(nxt->hdl,INTFC);
  libusb_close(nxt->hdl);
  libusb_unref_device(nxt->dev);
  free(nxt);

  return NXT_OK;
}

/**
 * Check if NXT is in initial download, or reset, mode.
 */
int
nxt_in_reset_mode(nxt_t *nxt) {
  return nxt->is_in_reset_mode;
}

/**
 * Send buffer to NXT.
 */
nxt_error_t
nxt_send_buf(nxt_t *nxt, char *buf, int len) {
  int numXfer;
  int ret = libusb_bulk_transfer(nxt->hdl, EP_OUT, (unsigned char*)buf, len, &numXfer, 0);
  
  if (ret<0) return NXT_USB_WRITE_ERROR;
  return NXT_OK;
}

/**
 * Send a string to NXT.
 */
nxt_error_t
nxt_send_str(nxt_t *nxt, char *str) {
  return nxt_send_buf(nxt, str, strlen(str));
}

/**
 * Read data from NXT into buffer.
 */
nxt_error_t
nxt_recv_buf(nxt_t *nxt, char *buf, int len) {
  int numXfer;
  int ret = libusb_bulk_transfer(nxt->hdl, EP_IN, (unsigned char*)buf, len, &numXfer, 0);
  
  if (ret<0) return NXT_USB_READ_ERROR;
  return NXT_OK;
}
