#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gphoto2.h>
#include <gphoto2-port.h>

#ifdef ENABLE_NLS
#  include <libintl.h>
#  undef _
#  define _(String) dgettext (PACKAGE, String)
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define _(String) (String)
#    define N_(String) (String)
#  endif
#else
#  define _(String) (String)
#  define N_(String) (String)
#endif

#include "dc240.h"
#include "library.h"

int
camera_id (CameraText *id) 
{
	strcpy(id->text, "kodak-dc240");

	return (GP_OK);
}

struct camera_to_usb {
	char *name;
	unsigned short idVendor;
	unsigned short idProduct;
} camera_to_usb[] = {
	{ "Kodak DC240", 0x040A, 0x0120 },
	{ "Kodak DC280", 0x040A, 0x0130 },
	{ "Kodak DC3400", 0x040A, 0x0132 },
	{ "Kodak DC5000", 0x040A, 0x0131 },
        { NULL, 0, 0 }
};

/*
  Abilities are based upon what we can do with a DC240.
  Later cameras have a superset of the DC240 feature and are not
  currently supported.
 */
int
camera_abilities (CameraAbilitiesList *list) 
{
	CameraAbilities a;
        int i;

        for (i = 0; camera_to_usb[i].name; i++)
        {
            strcpy(a.model, camera_to_usb[i].name);
	    a.status = GP_DRIVER_STATUS_PRODUCTION;
            a.port     = GP_PORT_SERIAL | GP_PORT_USB;
            a.speed[0] = 9600;
            a.speed[1] = 19200;
            a.speed[2] = 38400;
            a.speed[3] = 57600;
            a.speed[4] = 115200;
            a.speed[5] = 0;
            a.usb_vendor  = camera_to_usb[i].idVendor;
            a.usb_product = camera_to_usb[i].idProduct;
            a.operations        = 	GP_OPERATION_CAPTURE_IMAGE;
            a.file_operations   = 	GP_FILE_OPERATION_DELETE | 
                                        GP_FILE_OPERATION_PREVIEW;
            a.folder_operations = 	GP_FOLDER_OPERATION_NONE;
            
            gp_abilities_list_append(list, a);
        }
	return (GP_OK);
}

static int
camera_exit (Camera *camera) 
{
	dc240_close (camera);
	
	return (GP_OK);
}

static int
folder_list_func (CameraFilesystem *fs, const char *folder, CameraList *list,
		  void *data) 
{
	Camera *camera = data;

	return (dc240_get_folders (camera, list, folder));
}

static int
file_list_func (CameraFilesystem *fs, const char *folder, CameraList *list,
		void *data) 
{
	Camera *camera = data;

	return (dc240_get_filenames (camera, list, folder));
}

static int
get_file_func (CameraFilesystem *fs, const char *folder, const char *filename,
	       CameraFileType type, CameraFile *file, void *data) 
{
	Camera *camera = data;

	switch (type) {
	case GP_FILE_TYPE_NORMAL:
		return (dc240_file_action (camera, DC240_ACTION_IMAGE, file,
					   folder, filename));
	case GP_FILE_TYPE_PREVIEW:
		return (dc240_file_action (camera, DC240_ACTION_PREVIEW, file,
					   folder, (char*) filename));
	default:
		return (GP_ERROR_NOT_SUPPORTED);
	}
}

static int
delete_file_func (CameraFilesystem *fs, const char *folder,
		  const char *filename, void *data)
{
	Camera *camera = data;

	return (dc240_file_action (camera, DC240_ACTION_DELETE, NULL, folder, 
    				   filename));
}

static int
camera_capture (Camera *camera, CameraCaptureType type,
		CameraFilePath *path) 
{
	if (type != GP_CAPTURE_IMAGE)
		return (GP_ERROR_NOT_SUPPORTED);

	return dc240_capture(camera, path);
}

static int
camera_summary (Camera *camera, CameraText *summary) 
{
	strcpy(summary->text, _("No summary information."));

	return (GP_OK);
}

static int
camera_manual (Camera *camera, CameraText *manual) 
{
	strcpy(manual->text, _("No Manual Available"));

	return (GP_OK);
}

static int
camera_about (Camera *camera, CameraText *about) 
{
	strcpy (about->text, 
		_("Kodak DC240 Camera Library\n"
		"Scott Fritzinger <scottf@gphoto.net>\n"
		"Camera Library for the Kodak DC240, DC280, DC3400 and DC5000 cameras.\n"
		"Rewritten and updated for gPhoto2."));

	return (GP_OK);
}

int
camera_init (Camera *camera) 
{
	int ret, selected_speed = 0;
	GPPortSettings settings;
	
	/* First, set up all the function pointers */
	camera->functions->exit             = camera_exit;
	camera->functions->capture          = camera_capture;
	camera->functions->summary          = camera_summary;
	camera->functions->manual           = camera_manual;
	camera->functions->about            = camera_about;

	/* Set up the CameraFilesystem */
	gp_filesystem_set_list_funcs (camera->fs, file_list_func,
				      folder_list_func, camera);
	gp_filesystem_set_file_funcs (camera->fs, get_file_func,
				      delete_file_func, camera);

	ret = gp_port_settings_get (camera->port, &settings);
	if (ret < 0)
		return (ret);
	switch (camera->port->type) {
	case GP_PORT_SERIAL:

		/* Remember the selected speed */
		selected_speed = settings.serial.speed;

		settings.serial.speed    = 9600;
		settings.serial.bits     = 8;
		settings.serial.parity   = 0;
		settings.serial.stopbits = 1;
		break;
	case GP_PORT_USB:
		settings.usb.inep       = 0x82;
		settings.usb.outep      = 0x01;
		settings.usb.config     = 1;
		settings.usb.interface  = 0;
		settings.usb.altsetting = 0;
		break;
	default:
		return (GP_ERROR_UNKNOWN_PORT);
	}
	
	ret = gp_port_settings_set (camera->port, settings);
	if (ret < 0)
		return (ret);

	ret = gp_port_timeout_set (camera->port, TIMEOUT);
	if (ret < 0)
		return (ret);

	if (camera->port->type == GP_PORT_SERIAL) {
		/* Reset the camera to 9600 */
		gp_port_send_break(camera->port, 1);

		/* Wait for it to reset */
		GP_SYSTEM_SLEEP(1500);

		ret = dc240_set_speed (camera, selected_speed);
		if (ret < 0)
			return (ret);
	}

	/* Open the CF card */
	ret = dc240_open (camera);
	if (ret < 0)
		return (ret);
	
	ret = dc240_packet_set_size (camera, HPBS+2);
	if (ret < 0)
		return (ret);

	return (GP_OK);
}
