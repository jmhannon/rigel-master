#include "qsiapi.h"
#include "QSIError.h"
#include <fitsio.h>
#include <tiffio.h>

// char = int8
// short = int16
// int = int32
// long = int64
// long long = int64
void AdjustImage(unsigned short * buffer, int cols, int rows, unsigned char * out);

class Camera {
public:
	Camera();
	~Camera();
	const char *getInfo();
	void takePicture();
	pdl* test();
private:
	QSICamera cam;
	std::string info;

};

int WriteTIFF(unsigned short * buffer, int cols, int rows, char * filename)
{
	TIFF *image;
	unsigned char out[cols*rows];

	AdjustImage(buffer, cols, rows, out);

	// Open the TIFF file
	if((image = TIFFOpen(filename, "w")) == NULL)
	{
		printf("Could not open %s for writing\n", filename);
		exit(1);
	}

	// We need to set some values for basic tags before we can add any data
	TIFFSetField(image, TIFFTAG_IMAGEWIDTH, cols);
	TIFFSetField(image, TIFFTAG_IMAGELENGTH, rows);
	TIFFSetField(image, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(image, TIFFTAG_SAMPLESPERPIXEL, 1);
	TIFFSetField(image, TIFFTAG_ROWSPERSTRIP, 1);

	TIFFSetField(image, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	TIFFSetField(image, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
	TIFFSetField(image, TIFFTAG_FILLORDER, FILLORDER_LSB2MSB);
	TIFFSetField(image, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

	TIFFSetField(image, TIFFTAG_XRESOLUTION, 150.0);
	TIFFSetField(image, TIFFTAG_YRESOLUTION, 150.0);
	TIFFSetField(image, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);

	// Write the information to the file
	for (int y = 0; y < rows; y++)
	{
		TIFFWriteScanline(image, &out[cols*y], y);
	}

	// Close the file
	TIFFClose(image);
	return 0;
}

void AdjustImage(unsigned short * buffer, int x, int y, unsigned char * out)
{
	//
	// adjust the image to better display and
	// covert to a byte array
	//
	// Compute the average pixel value and the standard deviation
	double avg = 0;
	double total = 0;
	double deltaSquared = 0;
	double std = 0;

	for (int j = 0; j < y; j++)
		for (int i = 0; i < x; i++)
			total += (double)buffer[((j * x) + i)];

	avg = total / (x * y);

	for (int j = 0; j < y; j++)
		for (int i = 0; i < x; i++)
			deltaSquared += pow((avg - buffer[((j * x) + i)]), 2);

	std = sqrt(deltaSquared / ((x * y) - 1));

	// re-scale scale pixels to three standard deviations for display
	double minVal = avg - std*3;
	if (minVal < 0) minVal = 0;
	double maxVal = avg + std*3;
	if (maxVal > 65535) maxVal = 65535;
	double range = maxVal - minVal;
	if (range == 0)
		range = 1;
	double spread = 65535 / range;
	//
	// Copy image to bitmap for display and scale during the copy
	//
	int pix;
	double pl;
	unsigned char level;

	for (int j = 0; j < y; j++)
	{
		for (int i = 0; i < x; i++)
		{
			pix = ((j * x) + i);
			pl = (double)buffer[pix];
			// Spread out pixel values for better veiwing
			pl = (pl - minVal) * spread;
			// Scale pixel value
			pl = (pl*255)/65535;
			if (pl > 255) pl = 255;
			//
			level = (unsigned char)pl;
			out[pix] = level;
		}
	}
	return;
}

int	WriteFITS(unsigned short *buffer, int cols, int rows, const char *filename)
{
	int	status = 0;
	fitsfile	*fits;
	unlink(filename);
	fits_create_file(&fits, filename, &status);
	if (status) {
		std::cerr << "cannot create file " << filename << std::endl;
		return -1;
	}
	long	naxes[2] = { cols, rows };
	fits_create_img(fits, SHORT_IMG, 2, naxes, &status);
	long	fpixel[2] = { 1, 1 };
	fits_write_pix(fits, TUSHORT, fpixel, cols * rows, buffer, &status);
	fits_close_file(fits, &status);
	return 0;
}

pdl* Camera::test()
{
	// return getpdl();
	pdl *p = PDL->pdlnew();
	PDL_Indx dims[] = {5,5};
	PDL->setdims (p, dims, 2);  /* set dims */
	p->datatype = PDL_US;         /* and data type */
	PDL->allocdata (p);             /* allocate the data chunk */

	PDL_Ushort *dataf = (PDL_Ushort *) p->data;
	PDL_Indx i; /* dimensions might be 64bits */

	for (i=0;i<5*5;i++)
		dataf[i] = i; /* the data must be initialized ! */
	return p;
}

void Camera::takePicture()
{
	short binX;
	short binY;
	long xsize;
	long ysize;
	long startX;
	long startY;
	int result;

	cam.put_BinX(1);
	cam.put_BinY(1);
	// Get the dimensions of the CCD
	cam.get_CameraXSize(&xsize);
	cam.get_CameraYSize(&ysize);
	printf("camera size %ld x %ld\n", xsize, ysize);
	// Set the exposure to a full frame
	cam.put_StartX(0);
	cam.put_StartY(0);
	cam.put_NumX(xsize);
	cam.put_NumY(ysize);

	cam.put_PreExposureFlush(QSICamera::FlushNormal);
	cam.put_ManualShutterMode(false);

	bool enabled;
	cam.get_ManualShutterMode(&enabled);
	if (enabled)
	{
		printf("Manual shutter mode enabled\n");
		result = cam.put_ManualShutterOpen(true);
		if (result != 0)
		{
			printf("Failed to open shutter!\n");
		}
	} else {
		printf("Auto shutter mode enabled\n");
	}

	bool imageReady = false;
	printf("starting exposure\n");
	result = cam.StartExposure(20, true);
	if (result != 0)
	{
		printf("StartExposure failed: %d\n", result);
	}
	// Poll for image completed
	cam.get_ImageReady(&imageReady);
	while(!imageReady)
	{
		usleep(500);
		cam.get_ImageReady(&imageReady);
	}
	printf("done exposure\n");

	int x,y,z;
	// Get the image dimensions to allocate an image array
	cam.get_ImageArraySize(x, y, z);
	unsigned short* image = new unsigned short[x * y];
	// Retrieve the pending image from the camera
	result = cam.get_ImageArray(image);
	if (result != 0)
	{
		std::cout << "get_ImageArray error \n";
		std::string last("");
		cam.get_LastError(last);
		std::cout << last << "\n";
	}

	printf("saving\n");

	WriteFITS(image, x, y, "/tmp/picture.fits");
	WriteTIFF(image, x, y, "/tmp/picture.tiff");
	//vips im_copy qsiimage0.tif junk.png

	printf("free\n");
	delete [] image;
	printf("finished\n");
}

const char *Camera::getInfo()
{
	int iNumFound;
	std::string camSerial[QSICamera::MAXCAMERAS];
	std::string camDesc[QSICamera::MAXCAMERAS];
	std::string tmp("");

	info = "QSI SDK Version: ";
	cam.get_DriverInfo(tmp);
	info.append(tmp);
	info.append("\n");

	cam.get_AvailableCameras(camSerial, camDesc, iNumFound);

	if (iNumFound < 1)
	{
		info.append("No cameras found\n");
		return info.c_str();
	}

	info.append("Serial# ");
	info.append(camSerial[0]);
	info.append("\n");

	/*for (int i = 0; i < iNumFound; i++)
	{
		std::cout << camSerial[i] << ":" << camDesc[i] << "\n";
	}*/

	cam.put_SelectCamera(camSerial[0]);

	cam.put_IsMainCamera(true);
	// Connect to the selected camera and retrieve camera parameters
	if (cam.put_Connected(true) == 0)
	{
		info.append("Camera connected.\n");
	} else {
		info.append("failed to connect to camera.\n");
		return info.c_str();
	}

	// Get Model Number
	tmp.clear();
	std::string modelNumber;
	cam.get_ModelNumber(modelNumber);
	info.append("Model: ");
	info.append(modelNumber);
	info.append("\n");

	// Get Camera Description
	tmp.clear();
	cam.get_Description(tmp);
	info.append("Descr: ");
	info.append(tmp);
	info.append("\n");


	bool canSetTemp;
	cam.get_CanSetCCDTemperature(&canSetTemp);
	if (canSetTemp)
	{
		info.append("Temperature can be set\n");
		double temp;
		cam.get_CCDTemperature( &temp );
		char buf[100];
		snprintf(buf, sizeof(buf), "%.2f", temp);
		info.append("Current temp: ");
		info.append(buf);
		info.append("\n");
	}

	if (modelNumber.substr(0,1) == "6")
	{
		info.append("Can set Readout Seed\n");
		cam.put_ReadoutSpeed(QSICamera::FastReadout);
	}

	bool hasFilters;
	cam.get_HasFilterWheel(&hasFilters);
	if ( hasFilters)
	{
		info.append("Has filter wheel\n");
		int x = cam.put_Position(0);
		printf("set filter 0 = %d\n", x);
		if (x != 0)
		{
			std::string x;
			cam.get_LastError(x);
			printf(x.c_str());
			printf("\n");
		}
	}

	bool hasShutter;
	cam.get_HasShutter(&hasShutter);
	if (hasShutter)
	{
		info.append("Has shutter\n");
	}

	return info.c_str();
}

Camera::~Camera()
{
	printf("Camera DONE\n");
	cam.put_Connected(false);
}

Camera::Camera()
{
	printf("Camera INIT\n");
	cam.put_UseStructuredExceptions(false);
}

