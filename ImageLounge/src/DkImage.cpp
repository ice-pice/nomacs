/*******************************************************************************************************
 DkImage.cpp
 Created on:	21.04.2011
 
 nomacs is a fast and small image viewer with the capability of synchronizing multiple instances
 
 Copyright (C) 2011-2013 Markus Diem <markus@nomacs.org>
 Copyright (C) 2011-2013 Stefan Fiel <stefan@nomacs.org>
 Copyright (C) 2011-2013 Florian Kleber <florian@nomacs.org>

 This file is part of nomacs.

 nomacs is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 nomacs is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 *******************************************************************************************************/

#include "DkImage.h"
#include "DkNoMacs.h"
#include <QPluginLoader>

namespace nmc {

#ifdef WIN32

bool wCompLogic(const std::wstring & lhs, const std::wstring & rhs) {
	return StrCmpLogicalW(lhs.c_str(),rhs.c_str()) < 0;
	//return true;
}

bool compLogicQString(const QString & lhs, const QString & rhs) {
#if QT_VERSION < 0x050000
	return wCompLogic(lhs.toStdWString(), rhs.toStdWString());
	//return true;
#else
	return wCompLogic((wchar_t*)lhs.utf16(), (wchar_t*)rhs.utf16());	// TODO: is this nice?
#endif
}
#else
bool compLogicQString(const QString & lhs, const QString & rhs) {

	//// check if the filenames are just numbers
	//bool isNum;
	//int lhn = lhs.left(lhs.lastIndexOf(".")).toInt(&isNum);
	//qDebug() << "lhs dot idx: " << lhs.lastIndexOf(".");
	//if (isNum) {
	//	int rhn = rhs.left(rhs.lastIndexOf(".")).toInt(&isNum);
	//	qDebug() << "left is a number...";

	//	if (isNum) {
	//		qDebug() << "comparing numbers...";
	//		return lhn < rhn;
	//	}
	//}

	// number compare
	QRegExp r("\\d+");

	if (lhs.indexOf(r) >= 0) {
//		long long lhn = r.cap().toLongLong();
		QString lhstr = r.cap();

		// we don't just want to find two numbers
		// but we want them to be at the same position
		if (rhs.indexOf(r) >= 0 && r.indexIn(lhs) == r.indexIn(rhs))
		{
			QString rhstr = r.cap();
			if(rhstr.size() > lhstr.size())
			{
				while(rhstr.size() != lhstr.size())
					lhstr = "0" + lhstr;
			}
			else
			{
				while(rhstr.size() != lhstr.size())
					rhstr = "0" + rhstr;
			}
			return lhstr < rhstr;
		}

	}

	return lhs.localeAwareCompare(rhs) < 0;
}

#endif

bool compDateCreated(const QFileInfo& lhf, const QFileInfo& rhf) {

	return lhf.created() < rhf.created();
}

bool compDateCreatedInv(const QFileInfo& lhf, const QFileInfo& rhf) {

	return !compDateCreated(lhf, rhf);
}

bool compDateModified(const QFileInfo& lhf, const QFileInfo& rhf) {

	return lhf.lastModified() < rhf.lastModified();
}

bool compDateModifiedInv(const QFileInfo& lhf, const QFileInfo& rhf) {

	return !compDateModified(lhf, rhf);
}

bool compFilename(const QFileInfo& lhf, const QFileInfo& rhf) {

	return compLogicQString(lhf.fileName(), rhf.fileName());
}

bool compFilenameInv(const QFileInfo& lhf, const QFileInfo& rhf) {

	return !compFilename(lhf, rhf);
}

bool compRandom(const QFileInfo& lhf, const QFileInfo& rhf) {

	return qrand() % 2;
}


// well this is pretty shitty... but we need the filter without description too
QStringList DkImageLoader::fileFilters = QStringList();

// formats we can save
QStringList DkImageLoader::saveFilters = QStringList();

// formats we can load
QStringList DkImageLoader::openFilters = QStringList();

DkMetaData DkImageLoader::imgMetaData = DkMetaData();

// Basic loader and image edit class --------------------------------------------------------------------
DkBasicLoader::DkBasicLoader(int mode) {
	this->mode = mode;
	training = false;
	pageIdxDirty = false;
	numPages = 1;
	pageIdx = 1;
	loader = no_loader;
}

/**
 * This function loads the images.
 * @param file the image file that should be loaded.
 * @return bool true if the image could be loaded.
 **/ 
bool DkBasicLoader::loadGeneral(QFileInfo file, bool rotateImg, bool fast) {

	bool imgLoaded = false;
	
	if (file.isSymLink())
		this->file = file.symLinkTarget();
	else
		this->file = file;
	
	QString newSuffix = this->file.suffix();

	QImage oldImg = qImg;
#ifdef WITH_OPENCV
	cv::Mat oldMat = cvImg;
#endif
	release();

	if (pageIdxDirty)
		imgLoaded = loadPage();

	// identify raw images:
	//newSuffix.contains(QRegExp("(nef|crw|cr2|arw|rw2|mrw|dng)", Qt::CaseInsensitive)))

	QList<QByteArray> qtFormats = QImageReader::supportedImageFormats();
	QString suf = this->file.suffix().toLower();

	// default Qt loader
	// here we just try those formats that are officially supported
	if (!imgLoaded && qtFormats.contains(suf.toStdString().c_str())) {

		// if image has Indexed8 + alpha channel -> we crash... sorry for that
		imgLoaded = qImg.load(this->file.absoluteFilePath());
		if (imgLoaded) loader = qt_loader;
	}

	// PSD loader
	if (!imgLoaded) {

		imgLoaded = loadPSDFile(this->file);
		if (imgLoaded) loader = psd_loader;
	}
	// WEBP loader
	if (!imgLoaded) {

		imgLoaded = loadWebPFile(this->file);
		if (imgLoaded) loader = webp_loader;
	}

	// RAW loader
	if (!imgLoaded && !qtFormats.contains(suf.toStdString().c_str())) {
		
		// TODO: sometimes (e.g. _DSC6289.tif) strange opencv errors are thrown - catch them!
		// load raw files
		imgLoaded = loadRawFile(this->file, fast);
		if (imgLoaded) loader = raw_loader;
	}

	// default Qt loader
	if (!imgLoaded && !newSuffix.contains(QRegExp("(roh)", Qt::CaseInsensitive))) {

		QFile fileHandle(this->file.absoluteFilePath());
		fileHandle.open(QIODevice::ReadOnly);

		// if we first load files to buffers, we can additionally load images with wrong extensions (rainer bugfix : )
		// TODO: add warning here
		imgLoaded = qImg.loadFromData(fileHandle.readAll());
		if (imgLoaded) loader = qt_loader;
	}  

	// this loader is a bit buggy -> be carefull
	if (!imgLoaded && newSuffix.contains(QRegExp("(roh)", Qt::CaseInsensitive))) {
		imgLoaded = loadRohFile(this->file.absoluteFilePath());
		if (imgLoaded) loader = roh_loader;

	} 
	//if (!imgLoaded && (training || file.suffix().contains(QRegExp("(hdr)", Qt::CaseInsensitive)))) {

	//	// load hdr here...
	//	if (imgLoaded) loader = hdr_loader;
	//} 

	// ok, play back the old images
	if (!imgLoaded) {
		qImg = oldImg;
#ifdef WITH_OPENCV
		cvImg = oldMat;
#endif
	}

	// tiff things
	if (imgLoaded && !pageIdxDirty)
		indexPages(file);
	pageIdxDirty = false;

	if (imgLoaded && rotateImg && !DkSettings::metaData.ignoreExifOrientation) {
		DkMetaData imgMetaData(file);		
		int orientation = imgMetaData.getOrientation();

		if (!imgMetaData.isTiff() && !DkSettings::metaData.ignoreExifOrientation)
			rotate(orientation);
	}

	return imgLoaded;
}


/**
 * Loads special RAW files that are generated by the Hamamatsu camera.
 * @param fileName the filename of the file to be loaded.
 * @return bool true if the file could be loaded.
 **/ 
bool DkBasicLoader::loadRohFile(QString fileName){

	bool imgLoaded = true;

	void *pData;
	FILE * pFile;
	unsigned char test[2];
	unsigned char *buf;
	int rohW = 4000;
	int rohH = 2672;
	pData = malloc(sizeof(unsigned char) * (rohW*rohH*2));
	buf = (unsigned char *)malloc(sizeof(unsigned char) * (rohW*rohH));

	try{
		pFile = fopen (fileName.toStdString().c_str(), "rb" );

		fread(pData, 2, rohW*rohH, pFile);

		fclose(pFile);

		for (long i=0; i < (rohW*rohH); i++){
		
			test[0] = ((unsigned char*)pData)[i*2];
			test[1] = ((unsigned char*)pData)[i*2+1];
			test[0] = test[0] >> 4;
			test[0] = test[0] & 15;
			test[1] = test[1] << 4;
			test[1] = test[1] & 240;

			buf[i] = (test[0] | test[1]);
		
		}

		qImg = QImage((const uchar*) buf, rohW, rohH, QImage::Format_Indexed8);

		if (qImg.isNull())
			throw DkFileException("sorry, the roh file is empty...", __LINE__, __FILE__);

		//img = img.copy();
		QVector<QRgb> colorTable;

		for (int i = 0; i < 256; i++)
			colorTable.push_back(QColor(i, i, i).rgb());
		qImg.setColorTable(colorTable);

	} catch(...) {
		imgLoaded = false;
	}
	
	free(buf);
	free(pData);

	return imgLoaded;

}

/**
 * Loads the RAW file specified.
 * Note: nomacs needs to be compiled with OpenCV and LibRaw in
 * order to enable RAW file loading.
 * @param file the file to be loaded.
 * @return bool true if the file could be loaded.
 **/ 
bool DkBasicLoader::loadRawFile(QFileInfo file, bool fast) {

	bool imgLoaded = false;

	try {

#ifdef WITH_OPENCV

		LibRaw iProcessor;
		QImage image;
		int orientation = 0;

		//use iprocessor from libraw to read the data
		int error = iProcessor.open_file(file.absoluteFilePath().toStdString().c_str());

		if (error != LIBRAW_SUCCESS)
			return false;

		//// (-w) Use camera white balance, if possible (otherwise, fallback to auto_wb)
		//iProcessor.imgdata.params.use_camera_wb = 1;
		//// (-a) Use automatic white balance obtained after averaging over the entire image
		//iProcessor.imgdata.params.use_auto_wb = 1;
		//// (-q 3) Adaptive homogeneity-directed de-mosaicing algorithm (AHD)
		//iProcessor.imgdata.params.user_qual = 3;
		//iProcessor.imgdata.params.output_tiff = 1;
		////iProcessor.imgdata.params.four_color_rgb = 1;
		////iProcessor.imgdata.params.output_color = 1; //sRGB  (0...raw)
		//// RAW data filtration mode during data unpacking and post-processing
		//iProcessor.imgdata.params.filtering_mode = LIBRAW_FILTERING_AUTOMATIC;
		int tM = qMax(iProcessor.imgdata.thumbnail.twidth, iProcessor.imgdata.thumbnail.twidth);
		// TODO: check actual screen resolution
		qDebug() << "max thumb size: " << tM;
				
		if (fast || DkSettings::resources.loadRawThumb == DkSettings::raw_thumb_always ||
			(DkSettings::resources.loadRawThumb == DkSettings::raw_thumb_if_large && tM >= 1920)) {
			
			// crashes here if image is broken
			int err = iProcessor.unpack_thumb();
			char* tPtr = iProcessor.imgdata.thumbnail.thumb;

			if (!err && tPtr) {

				QImage tmp;
				tmp.loadFromData((const uchar*) tPtr, iProcessor.imgdata.thumbnail.tlength);

				if (!tmp.isNull()) {
					imgLoaded = true;
					qImg = tmp;
					qDebug() << "[RAW] I loaded the RAW's thumbnail";

					return imgLoaded;
				}
				else
					qDebug() << "qt could not load the thumb";
			}
			else
				qDebug() << "error unpacking the thumb...";
		}

		qDebug() << "[RAW] loading full raw file";


		//unpack the data
		error = iProcessor.unpack();
#ifndef LIBRAW_VERSION_13	// fixes a bug specific to libraw 13 - version call is UNTESTED
		iProcessor.raw2image();
#endif
		if (error != LIBRAW_SUCCESS)
			return false;

		//iProcessor.dcraw_process();
		//iProcessor.dcraw_ppm_tiff_writer("test.tiff");

		unsigned short cols = iProcessor.imgdata.sizes.width,//.raw_width,
			rows = iProcessor.imgdata.sizes.height;//.raw_height;

		Mat rawMat, rgbImg;

		// modifications sequence for changing from raw to rgb:
		// 1. normalize according to black point and dynamic range
		// 2. demosaic
		// 3. white balance
		// 4. color correction
		// 5. gamma correction

		//GENERAL TODO
		//check if the corrections (black, white point gamma correction) are done in the correct order
		//check if the specific corrections are different regarding different camera models
		//find out some general specifications of the most important raw formats

		//qDebug() << "----------------";
		//qDebug() << "Bayer Pattern: " << QString::fromStdString(iProcessor.imgdata.idata.cdesc);
		//qDebug() << "Camera manufacturer: " << QString::fromStdString(iProcessor.imgdata.idata.make);
		//qDebug() << "Camera model: " << QString::fromStdString(iProcessor.imgdata.idata.model);
		//qDebug() << "canon_ev " << (float)iProcessor.imgdata.color.canon_ev;

		//debug outputs of the exif data read by libraw
		//qDebug() << "white: [%.3f %.3f %.3f %.3f]\n", iProcessor.imgdata.color.cam_mul[0],
		//	iProcessor.imgdata.color.cam_mul[1], iProcessor.imgdata.color.cam_mul[2],
		//	iProcessor.imgdata.color.cam_mul[3]);
		//qDebug() << "black: %i\n", iProcessor.imgdata.color.black);
		//qDebug() << "maximum: %.i %i\n", iProcessor.imgdata.color.maximum,
		//	iProcessor.imgdata.params.adjust_maximum_thr);
		//qDebug() << "gamma: %.3f %.3f %.3f %.3f %.3f %.3f\n",
		//	iProcessor.imgdata.params.gamm[0],
		//	iProcessor.imgdata.params.gamm[1],
		//	iProcessor.imgdata.params.gamm[2],
		//	iProcessor.imgdata.params.gamm[3],
		//	iProcessor.imgdata.params.gamm[4],
		//	iProcessor.imgdata.params.gamm[5]);

		//qDebug() << "----------------";

		if (strcmp(iProcessor.imgdata.idata.cdesc, "RGBG")) throw DkException("Wrong Bayer Pattern (not RGBG)\n", __LINE__, __FILE__);



		// 1. read raw image and normalize it according to dynamic range and black point
		
		//dynamic range is defined by maximum - black
		float dynamicRange = iProcessor.imgdata.color.maximum-iProcessor.imgdata.color.black;	// iProcessor.imgdata.color.channel_maximum[0]-iProcessor.imgdata.color.black;	// dynamic range

		if (iProcessor.imgdata.idata.filters) {

			rawMat = Mat(rows, cols, CV_32FC1);

			for (uint row = 0; row < rows; row++) {
				float *ptrRaw = rawMat.ptr<float>(row);

				for (uint col = 0; col < cols; col++) {

					int colorIdx = iProcessor.COLOR(row, col);
					ptrRaw[col] = (float)(iProcessor.imgdata.image[cols*(row) + col][colorIdx]);

					//correct the image values according the black point defined by the camera
					ptrRaw[col] -= iProcessor.imgdata.color.black;
					//normalize according the dynamic range
					ptrRaw[col] /= dynamicRange;
					ptrRaw[col] *= 65535;  // for conversion to 16U
				}
			}
			
			// 2. demosaic raw image
			rawMat.convertTo(rawMat,CV_16U);

			//cvtColor(rawMat, rgbImg, CV_BayerBG2RGB);
			unsigned long type = (unsigned long)iProcessor.imgdata.idata.filters;
			type = type & 255;

			//define bayer pattern
			if (type == 180) cvtColor(rawMat, rgbImg, CV_BayerBG2RGB);      //bitmask  10 11 01 00  -> 3(G) 2(B) 1(G) 0(R) -> RG RG RG
			//												                                                                  GB GB GB
			else if (type == 30) cvtColor(rawMat, rgbImg, CV_BayerRG2RGB);		//bitmask  00 01 11 10	-> 0 1 3 2
			else if (type == 225) cvtColor(rawMat, rgbImg, CV_BayerGB2RGB);		//bitmask  11 10 00 01
			else if (type == 75) cvtColor(rawMat, rgbImg, CV_BayerGR2RGB);		//bitmask  01 00 10 11
			else throw DkException("Wrong Bayer Pattern (not BG, RG, GB, GR)\n", __LINE__, __FILE__);
		}
		else {

			rawMat = Mat(rows, cols, CV_32FC3);
			rawMat.setTo(0);
			std::vector<Mat> rawCh;
			split(rawMat, rawCh);

			for (unsigned int row = 0; row < rows; row++) {
				float *ptrR = rawCh[0].ptr<float>(row);
				float *ptrG = rawCh[1].ptr<float>(row);
				float *ptrB = rawCh[2].ptr<float>(row);
				//float *ptrE = rawCh[3].ptr<float>(row);

				for (unsigned int col = 0; col < cols; col++) {

					int colorIdx = iProcessor.COLOR(row, col);
					ptrR[col] = (float)(iProcessor.imgdata.image[cols*(row) + col][0]);
					ptrR[col] -= iProcessor.imgdata.color.black;
					ptrR[col] /= dynamicRange;
					ptrR[col] *= 65535;  // for conversion to 16U

					ptrG[col] = (float)(iProcessor.imgdata.image[cols*(row) + col][1]);
					ptrG[col] -= iProcessor.imgdata.color.black;
					ptrG[col] /= dynamicRange;
					ptrG[col] *= 65535;  // for conversion to 16U

					ptrB[col] = (float)(iProcessor.imgdata.image[cols*(row) + col][2]);
					ptrB[col] -= iProcessor.imgdata.color.black;
					ptrB[col] /= dynamicRange;
					ptrB[col] *= 65535;  // for conversion to 16U

				}
			}
			merge(rawCh, rgbImg);
			rgbImg.convertTo(rgbImg, CV_16U);
		}

		rawMat.release();

		// 3.. 4., 5.: apply white balance, color correction and gamma 

		// get color correction matrix
		float colorCorrMat[3][4] = {};
		for(int i=0;i<3;i++) for(int j=0;j<4;j++) colorCorrMat[i][j] = iProcessor.imgdata.color.rgb_cam[i][j];

		// get camera white balance multipliers
		float mulWhite[4];
		mulWhite[0] = iProcessor.imgdata.color.cam_mul[0];
		mulWhite[1] = iProcessor.imgdata.color.cam_mul[1];
		mulWhite[2] = iProcessor.imgdata.color.cam_mul[2];
		mulWhite[3] = iProcessor.imgdata.color.cam_mul[3];

		//read gamma value and create gamma table
		float gamma = (float)iProcessor.imgdata.params.gamm[0];///(float)iProcessor.imgdata.params.gamm[1];
		float gammaTable[65536];
		for (int i = 0; i < 65536; i++) {
			gammaTable[i] = (float)(1.099f*pow((float)i/65535.0f, gamma)-0.099f);
		}

		// normalize white balance multipliers
		float w = (mulWhite[0] + mulWhite[1] + mulWhite[2] + mulWhite[3])/4.0f;
		float maxW = 1.0f;//mulWhite[0];

		//clipping according the camera model
		//if w > 2.0 maxW is 256, otherwise 512
		//tested empirically
		//check if it can be defined by some metadata settings?
		if (w > 2.0f)
			maxW = 256.0f;
		if (w > 2.0f && QString(iProcessor.imgdata.idata.make).compare("Canon", Qt::CaseInsensitive) == 0)
			maxW = 512.0f;	// some cameras would even need ~800 - why?

		//normalize white point
		mulWhite[0] /= maxW;
		mulWhite[1] /= maxW;
		mulWhite[2] /= maxW;
		mulWhite[3] /= maxW;

		if (mulWhite[3] == 0)
			mulWhite[3] = mulWhite[1];

		////DkUtils::printDebug(DK_MODULE, "----------------\n", (float)iProcessor.imgdata.color.maximum);
		////DkUtils::printDebug(DK_MODULE, "Bayer Pattern: %s\n", iProcessor.imgdata.idata.cdesc);
		////DkUtils::printDebug(DK_MODULE, "Camera manufacturer: %s\n", iProcessor.imgdata.idata.make);
		////DkUtils::printDebug(DK_MODULE, "Camera model: %s\n", iProcessor.imgdata.idata.model);
		////DkUtils::printDebug(DK_MODULE, "canon_ev %f\n", (float)iProcessor.imgdata.color.canon_ev);

		////DkUtils::printDebug(DK_MODULE, "white: [%.3f %.3f %.3f %.3f]\n", iProcessor.imgdata.color.cam_mul[0],
		////	iProcessor.imgdata.color.cam_mul[1], iProcessor.imgdata.color.cam_mul[2],
		////	iProcessor.imgdata.color.cam_mul[3]);
		////DkUtils::printDebug(DK_MODULE, "white (processing): [%.3f %.3f %.3f %.3f]\n", mulWhite[0],
		////	mulWhite[1], mulWhite[2],
		////	mulWhite[3]);
		////DkUtils::printDebug(DK_MODULE, "black: %i\n", iProcessor.imgdata.color.black);
		////DkUtils::printDebug(DK_MODULE, "maximum: %.i %i\n", iProcessor.imgdata.color.maximum,
		////	iProcessor.imgdata.params.adjust_maximum_thr);
		////DkUtils::printDebug(DK_MODULE, "----------------\n", (float)iProcessor.imgdata.color.maximum);



		//apply corrections
		std::vector<Mat> corrCh;
		split(rgbImg, corrCh);

		for (uint row = 0; row < rows; row++)
		{
			unsigned short *ptrR = corrCh[0].ptr<unsigned short>(row);
			unsigned short *ptrG = corrCh[1].ptr<unsigned short>(row);
			unsigned short *ptrB = corrCh[2].ptr<unsigned short>(row);

			for (uint col = 0; col < cols; col++)
			{
				//apply white balance correction
				int tempR = ptrR[col] * mulWhite[0];
				int tempG = ptrG[col] * mulWhite[1];
				int tempB = ptrB[col] * mulWhite[2];

				//apply color correction					
				int corrR = colorCorrMat[0][0] * tempR + colorCorrMat[0][1] * tempG  + colorCorrMat[0][2] * tempB;
				int corrG = colorCorrMat[1][0] * tempR + colorCorrMat[1][1] * tempG  + colorCorrMat[1][2] * tempB;
				int corrB = colorCorrMat[2][0] * tempR + colorCorrMat[2][1] * tempG  + colorCorrMat[2][2] * tempB;
				// without color correction: change above three lines to the bottom ones
				//int corrR = tempR;
				//int corrG = tempG;
				//int corrB = tempB;

				//clipping
				ptrR[col] = (corrR > 65535) ? 65535 : (corrR < 0) ? 0 : corrR;
				ptrG[col] = (corrG > 65535) ? 65535 : (corrG < 0) ? 0 : corrG;
				ptrB[col] = (corrB > 65535) ? 65535 : (corrB < 0) ? 0 : corrB;

				//apply gamma correction
				ptrR[col] = ptrR[col] <= 0.018f * 65535.0f ? (unsigned short)(ptrR[col]*(float)iProcessor.imgdata.params.gamm[1]/257.0f) :
					gammaTable[ptrR[col]] * 255;
				//									(1.099f*(float)(pow((float)ptrRaw[col], gamma))-0.099f);
				ptrG[col] = ptrG[col] <= 0.018f * 65535.0f ? (unsigned short)(ptrG[col]*(float)iProcessor.imgdata.params.gamm[1]/257.0f) :
					gammaTable[ptrG[col]] * 255;
				ptrB[col] = ptrB[col] <= 0.018f * 65535.0f ? (unsigned short)(ptrB[col]*(float)iProcessor.imgdata.params.gamm[1]/257.0f) :
					gammaTable[ptrB[col]] * 255;
			}
		}

		merge(corrCh, rgbImg);
		rgbImg.convertTo(rgbImg, CV_8U);
			
		// filter color noise withe a median filter
		if (DkSettings::resources.filterRawImages) {

			float isoSpeed = iProcessor.imgdata.other.iso_speed;
				
			if (isoSpeed > 0) {

				int winSize;
				if (isoSpeed > 6400) winSize = 13;
				else if (isoSpeed >= 3200) winSize = 11;
				else if (isoSpeed >= 2500) winSize = 9;
				else if (isoSpeed >= 400) winSize = 7;
				else winSize = 5;

				DkTimer dMed;			

				cvtColor(rgbImg, rgbImg, CV_RGB2YCrCb);
				split(rgbImg, corrCh);

				cv::medianBlur(corrCh[1], corrCh[1], winSize);
				cv::medianBlur(corrCh[2], corrCh[2], winSize);

				merge(corrCh, rgbImg);
				cvtColor(rgbImg, rgbImg, CV_YCrCb2RGB);

				qDebug() << "median blurred in: " << QString::fromStdString(dMed.getTotal()) << ", winSize: " << winSize;
			}
			else qDebug() << "median filter: unrecognizable ISO speed";
			
		}


		//Mat cropMat(rawMat, Rect(1, 1, rawMat.cols-1, rawMat.rows-1));
		//rawMat.release();
		//rawMat = cropMat;
		//rawMat.setTo(0);

		//normalize(rawMat, rawMat, 255, 0, NORM_MINMAX);

		//check the pixel aspect ratio of the raw image
		if (iProcessor.imgdata.sizes.pixel_aspect != 1.0f) {
			cv::resize(rgbImg, rawMat, Size(), (double)iProcessor.imgdata.sizes.pixel_aspect, 1.0f);
			rgbImg = rawMat;
		}

		//create the final image
		image = QImage(rgbImg.data, (int)rgbImg.cols, (int)rgbImg.rows, (int)rgbImg.step/*rgbImg.cols*3*/, QImage::Format_RGB888);

		//orientation is done in loadGeneral with libExiv
		//orientation = iProcessor.imgdata.sizes.flip;
		//switch (orientation) {
		//case 0: orientation = 0; break;
		//case 3: orientation = 180; break;
		//case 5:	orientation = -90; break;
		//case 6: orientation = 90; break;
		//}

		qImg = image.copy();
		//if (orientation!=0) {
		//	QTransform rotationMatrix;
		//	rotationMatrix.rotate((double)orientation);
		//	img = img.transformed(rotationMatrix);
		//}
		imgLoaded = true;

		iProcessor.recycle();

#else
		throw DkException("Not compiled using OpenCV - could not load any RAW image", __LINE__, __FILE__);
		qDebug() << "Not compiled using OpenCV - could not load any RAW image";
#endif
	} catch (...) {
		//// silently ignore, maybe it's not a raw image
		//qWarning() << "failed to load raw image...";
	}

	return imgLoaded;
}

#ifdef WITH_WEBP

bool DkBasicLoader::loadWebPFile(QFileInfo fileInfo) {

	QFile file(fileInfo.absoluteFilePath());
	file.open(QIODevice::ReadOnly);
	
	return decodeWebP(file.readAll());

}

bool DkBasicLoader::decodeWebP(const QByteArray& buffer) {
	qDebug() << "file exists: " << file.exists();
	qDebug() << "empty buffer: " << buffer.isEmpty();


	// retrieve the image features (size, alpha etc.)
	WebPBitstreamFeatures features;
	int error = WebPGetFeatures((const uint8_t*)buffer.data(), buffer.size(), &features);
	if (error) return false;

	uint8_t* webData = 0;

	if (features.has_alpha) {
		webData = WebPDecodeBGRA((const uint8_t*) buffer.data(), buffer.size(), &features.width, &features.height);
		if (!webData) return false;
		qImg = QImage(webData, (int)features.width, (int)features.height, QImage::Format_ARGB32);
	}
	else {
		webData = WebPDecodeRGB((const uint8_t*) buffer.data(), buffer.size(), &features.width, &features.height);
		if (!webData) return false;
		qImg = QImage(webData, (int)features.width, (int)features.height, features.width*3, QImage::Format_RGB888);
	}

	// clone the image so we own the buffer
	qImg = qImg.copy();
	if (webData) free(webData);

	qDebug() << "buffer size: " << buffer.size();

	return true;
}

#endif

bool DkBasicLoader::loadPSDFile(QFileInfo fileInfo) {

	QFile file(fileInfo.absoluteFilePath());
	file.open(QIODevice::ReadOnly);

	QPsdHandler psdHandler;
	psdHandler.setDevice(&file);	// QFile is an IODevice
	//psdHandler.setFormat(fileInfo.suffix().toLocal8Bit());
	
	if (psdHandler.canRead(&file))
		return psdHandler.read(&this->qImg);
		
	return false;
}

void DkBasicLoader::indexPages(const QFileInfo& fileInfo) {

	// reset counters
	numPages = 1;
	pageIdx = 1;

#ifdef WITH_LIBTIFF

	// for now we just support tiff's
	if (!fileInfo.suffix().contains(QRegExp("(tif|tiff)", Qt::CaseInsensitive)))
		return;

	// first turn off nasty warning/error dialogs - (we do the GUI : )
	TIFFErrorHandler oldErrorHandler, oldWarningHandler;
	oldWarningHandler = TIFFSetWarningHandler(NULL);
	oldErrorHandler = TIFFSetErrorHandler(NULL); 

	DkTimer dt;
	TIFF* tiff = TIFFOpen(this->file.absoluteFilePath().toLatin1(), "r");

	if (!tiff) 
		return;

	// libtiff example
	int dircount = 0;

	do {
		dircount++;

	} while (TIFFReadDirectory(tiff));

	numPages = dircount;

	if (numPages > 1)
		pageIdx = 1;

	qDebug() << dircount << " TIFF directories... " << QString::fromStdString(dt.getTotal());
	TIFFClose(tiff);

	TIFFSetWarningHandler(oldWarningHandler);
	TIFFSetWarningHandler(oldErrorHandler);
#endif

}

bool DkBasicLoader::loadPage(int skipIdx) {

	bool imgLoaded = false;

#ifdef WITH_LIBTIFF
	pageIdx += skipIdx;

	// <= 1 since first page is loaded using qt
	if (pageIdx > numPages || pageIdx <= 1)
		return imgLoaded;

	// first turn off nasty warning/error dialogs - (we do the GUI : )
	TIFFErrorHandler oldErrorHandler, oldWarningHandler;
	oldWarningHandler = TIFFSetWarningHandler(NULL);
	oldErrorHandler = TIFFSetErrorHandler(NULL); 

	DkTimer dt;
	TIFF* tiff = TIFFOpen(this->file.absoluteFilePath().toLatin1(), "r");

	if (!tiff)
		return imgLoaded;

	uint32 width = 0;
	uint32 height = 0;
	TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
	TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);

	// go to current directory
	for (int idx = 1; idx < pageIdx; idx++) {

		if (!TIFFReadDirectory(tiff))
			return false;
	}

	// init the qImage
	qImg = QImage(width, height, QImage::Format_ARGB32);

	const int stopOnError = 1;
	imgLoaded = TIFFReadRGBAImageOriented(tiff, width, height, reinterpret_cast<uint32 *>(qImg.bits()), ORIENTATION_TOPLEFT, stopOnError);

	if (imgLoaded) {
		for (uint32 y=0; y<height; ++y)
			convert32BitOrder(qImg.scanLine(y), width);
	}

	TIFFClose(tiff);

	TIFFSetWarningHandler(oldWarningHandler);
	TIFFSetWarningHandler(oldErrorHandler);
#endif

	return imgLoaded;
}

bool DkBasicLoader::setPageIdx(int skipIdx) {

	// do nothing if we don't have tiff pages
	if (numPages <= 1)
		return false;

	pageIdxDirty = false;

	int newPageIdx = pageIdx + skipIdx;

	if (newPageIdx > 0 && newPageIdx <= numPages) {
		pageIdxDirty = true;
		pageIdx = newPageIdx;
	}

	return pageIdxDirty;
}

void DkBasicLoader::convert32BitOrder(void *buffer, int width) {

#ifdef WITH_LIBTIFF
	// code from Qt QTiffHandler
	uint32 *target = reinterpret_cast<uint32 *>(buffer);
	for (int32 x=0; x<width; ++x) {
		uint32 p = target[x];
		// convert between ARGB and ABGR
		target[x] = (p & 0xff000000)
			| ((p & 0x00ff0000) >> 16)
			| (p & 0x0000ff00)
			| ((p & 0x000000ff) << 16);
	}
#endif
}

bool DkBasicLoader::save(QFileInfo fileInfo, QImage img, int compression) {

	bool saved = false;

	qDebug() << "extension: " << fileInfo.suffix();

	if (fileInfo.suffix().contains("webp", Qt::CaseInsensitive)) {
		saved = saveWebPFile(fileInfo, img, compression);
	}
	else if (!saved) {


		bool hasAlpha = DkImage::alphaChannelUsed(img);
		QImage sImg = img;
		
		if (!hasAlpha)
			sImg.convertToFormat(QImage::Format_RGB888);

		qDebug() << "img has alpha: " << (img.format() != QImage::Format_RGB888) << " img uses alpha: " << hasAlpha;

		QImageWriter* imgWriter = new QImageWriter(fileInfo.absoluteFilePath());
		imgWriter->setCompression(compression);
		imgWriter->setQuality(compression);
		saved = imgWriter->write(sImg);		// TODO: J2K crash detected
		delete imgWriter;
	}

	return saved;
}

#ifdef WITH_WEBP
bool DkBasicLoader::saveWebPFile(QFileInfo fileInfo, QImage img, int compression) {
	
	qDebug() << "format: " << img.format();

	QByteArray buffer;

	if (encodeWebP(buffer, img, compression)) {

		QFile file(fileInfo.absoluteFilePath());
		file.open(QIODevice::WriteOnly);
		file.write(buffer);
	
		return true;
	}

	return false;
}

bool DkBasicLoader::encodeWebP(QByteArray& buffer, QImage img, int compression, int speed) {
	
	// currently, guarantee that the image is a ARGB image
	if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_RGB888)
		img = img.convertToFormat(QImage::Format_RGB888);	// for now
	//char* buffer;
	//size_t bufSize;

	//if (compression < 0) {

	//	if (!img.hasAlphaChannel())
	//		qDebug() << "no alpha...";


	//	if (img.hasAlphaChannel())
	//		bufSize = WebPEncodeLosslessBGRA(reinterpret_cast<const uint8_t*>(img.constBits()), img.width(), img.height(), img.bytesPerLine(), reinterpret_cast<uint8_t**>(&buffer));
	//	// // without alpha there is something wrong...
	//	else
	//		bufSize = WebPEncodeLosslessRGB(reinterpret_cast<const uint8_t*>(img.constBits()), img.width(), img.height(), img.bytesPerLine(), reinterpret_cast<uint8_t**>(&buffer));
	//}
	//else {
	//	
	//	if (img.hasAlphaChannel())
	//		bufSize = WebPEncodeBGRA(reinterpret_cast<const uint8_t*>(img.constBits()), img.width(), img.height(), img.bytesPerLine(), compression, reinterpret_cast<uint8_t**>(&buffer));
	//	else
	//		bufSize = WebPEncodeRGB(reinterpret_cast<const uint8_t*>(img.constBits()), img.width(), img.height(), img.bytesPerLine(), compression, reinterpret_cast<uint8_t**>(&buffer));
	//}

	//if (!bufSize) return false;

	//QFile file(fileInfo.absoluteFilePath());
	//file.open(QIODevice::WriteOnly);
	//file.write(buffer, bufSize);
	//free(buffer);

	WebPConfig config;
	bool lossless = false;
	if (compression == -1) {
		compression = 100;
		lossless = true;
	}
	if (!WebPConfigPreset(&config, WEBP_PRESET_PHOTO, compression)) return false;
	if (lossless) config.lossless = 1;
	config.method = speed;

	WebPPicture webImg;
	if (!WebPPictureInit(&webImg)) return false;
	webImg.width = img.width();
	webImg.height = img.height();
	webImg.use_argb = true;		// we never use YUV
	//webImg.argb_stride = img.bytesPerLine();
	//webImg.argb = reinterpret_cast<uint32_t*>(img.bits());

	qDebug() << "speed method: " << config.method;

	int errorCode = 0;

	if (img.hasAlphaChannel())
		errorCode = WebPPictureImportBGRA(&webImg, reinterpret_cast<uint8_t*>(img.bits()), img.bytesPerLine());
	else
		errorCode = WebPPictureImportRGB(&webImg, reinterpret_cast<uint8_t*>(img.bits()), img.bytesPerLine());

	qDebug() << "import error: " << errorCode;

	// Set up a byte-writing method (write-to-memory, in this case):
	WebPMemoryWriter writer;
	WebPMemoryWriterInit(&writer);
	webImg.writer = WebPMemoryWrite;
	webImg.custom_ptr = &writer;

	int ok = WebPEncode(&config, &webImg);
	if (!ok || writer.size == 0) return false;

	buffer = QByteArray(reinterpret_cast<const char*>(writer.mem), (int)writer.size);	// does a deep copy
	WebPPictureFree(&webImg);

	return true;
}
#endif


// image editing --------------------------------------------------------------------
/**
 * This method rotates an image.
 * @param orientation the orientation in degree.
 **/ 
void DkBasicLoader::rotate(int orientation) {

	if (orientation == 0 || orientation == -1)
		return;

	QTransform rotationMatrix;
	rotationMatrix.rotate((double)orientation);
	qImg = qImg.transformed(rotationMatrix);

// TODO: test without OpenCV
#ifdef WITH_OPENCV

	if (!cvImg.empty()) {

		DkVector nSz = DkVector(cvImg.size());	// *0.5f?
		DkVector nSl = nSz;
		DkVector nSr = nSz;

		double angleRad = orientation*DK_RAD2DEG;
		int interpolation = (orientation % 90 == 0) ? INTER_NEAREST : INTER_CUBIC;

		// compute
		nSl.rotate(angleRad);
		nSl.abs();

		nSr.swap();
		nSr.rotate(angleRad);
		nSr.abs();
		nSr.swap();

		nSl = nSl.maxVec(nSr);

		DkVector center = nSl * 0.5f;

		Mat rotMat = getRotationMatrix2D(center.getCvPoint32f(), DK_RAD2DEG*angleRad, 1.0);

		// add a shift towards new center
		DkVector cDiff = center - (nSz * 0.5f);
		cDiff.rotate(angleRad);

		double *transl = rotMat.ptr<double>();
		transl[2] += (double)cDiff.x;
		transl[5] += (double)cDiff.y;

		// img in wrapAffine must not be overwritten
		Mat rImg = Mat(nSl.getCvSize(), cvImg.type());
		warpAffine(cvImg, rImg, rotMat, rImg.size(), interpolation, BORDER_CONSTANT/*, borderValue*/);
		cvImg = rImg;
	} 

#endif

}

/**
 * This function resizes an image.
 * if the img assigned is a NULL pointer, the currently loaded image is resized.
 * @param size	the target size (may be empty if factor != 1.0f)
 * @param factor the scale ratio (can be 1.0f if QSize is not empty)
 * @param img a pointer to the image that should be resized (if img == 0, the currently loaded image is resized)
 * @param interpolation the interpolation method
 * @param silent if true, no error dialog is shown
 **/ 
void DkBasicLoader::resize(QSize size, float factor, QImage* img, int interpolation, bool silent) {
	
	bool resizeLocal = (img) ? false : true;

	// resize my image, if the user did not specify anything
	if (img == 0)
		img = &qImg;
	
	// nothing to do
	if (img->size() == size && factor == 1.0f)
		return;

	if (factor != 1.0f)
		size = QSize(img->width()*factor, img->height()*factor);

	// attention: we do not define a maximum, however if the machine has too view RAM this function may crash
	if (size.width() < 1 || size.height() < 1 || img->isNull())
		return;

	Qt::TransformationMode iplQt;
	switch(interpolation) {
	case DkImage::ipl_nearest:	
	case DkImage::ipl_area:		iplQt = Qt::FastTransformation; break;
	case DkImage::ipl_linear:	
	case DkImage::ipl_cubic:		
	case DkImage::ipl_lanczos:	iplQt = Qt::SmoothTransformation; break;
	}
#ifdef WITH_OPENCV

	int ipl = CV_INTER_CUBIC;
	switch(interpolation) {
	case DkImage::ipl_nearest:	ipl = CV_INTER_NN; break;
	case DkImage::ipl_area:		ipl = CV_INTER_AREA; break;
	case DkImage::ipl_linear:	ipl = CV_INTER_LINEAR; break;
	case DkImage::ipl_cubic:	ipl = CV_INTER_CUBIC; break;
#ifdef DISABLE_LANCZOS
	case DkImage::ipl_lanczos:	ipl = CV_INTER_CUBIC; break;
#else
	case DkImage::ipl_lanczos:	ipl = CV_INTER_LANCZOS4; break;
#endif
	}

	Mat resizeImage;

	if (cvImg.empty() || !resizeLocal)
		resizeImage = DkImage::qImage2Mat(*img);
	else
		resizeImage = cvImg;

	try {
		// is the image convertible?
		if (resizeImage.empty()) {
			img->scaled(size, Qt::IgnoreAspectRatio, iplQt);
		}
		else {

			Mat tmp;
			cv::resize(resizeImage, tmp, cv::Size(size.width(), size.height()), 0, 0, ipl);
			resizeImage.release();

			if (!cvImg.empty() && resizeLocal)
				cvImg = tmp;

			qImg = DkImage::mat2QImage(tmp);
		}

	}catch (std::exception se) {

		if (!silent)
			DkNoMacs::dialog(tr("Sorry, the image is too large: %1").arg(DkImage::getBufferSize(size, 32)));

		return;
	}

#else

	img->scaled(size, Qt::IgnoreAspectRatio, iplQt);

#endif

}


/**
 * Releases the currently loaded images.
 **/ 
void DkBasicLoader::release() {

	// TODO: auto save routines here

	qImg = QImage();

#ifdef WITH_OPENCV
	cvImg.release();
#endif

}

// DkImageLoader -> is nomacs file handling routine --------------------------------------------------------------------
/**
 * Default constructor.
 * Creates a DkImageLoader instance with a given file.
 * @param file the file to be loaded.
 **/ 
DkImageLoader::DkImageLoader(QFileInfo file) {

	qRegisterMetaType<QFileInfo>("QFileInfo");
	loaderThread = new QThread;
	loaderThread->start();
	moveToThread(loaderThread);

	watcher = 0;
	// init the watcher
	//watcher = new QFileSystemWatcher();
	//connect(watcher, SIGNAL(fileChanged(QString)), this, SLOT(fileChanged(QString)));
	
	// this seems to be unnecessary complicated, but otherwise we create the watcher in different threads
	// (depending on if loadFile() is called threaded) which is not a very good ides
	connect(this, SIGNAL(updateFileWatcherSignal(QFileInfo)), this, SLOT(updateFileWatcher(QFileInfo)), Qt::QueuedConnection);

	dirWatcher = new QFileSystemWatcher();
	connect(dirWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(directoryChanged(QString)));

	folderUpdated = false;
	cFileIdx = 0;

	this->file = file;
	this->virtualFile = file;

	delayedUpdateTimer.moveToThread(loaderThread);
	delayedUpdateTimer.setSingleShot(true);
	connect(&delayedUpdateTimer, SIGNAL(timeout()), this, SLOT(directoryChanged()));
	timerBlockedUpdate = false;

	//saveDir = DkSettings::global.lastSaveDir;	// loading save dir is obsolete ?!
	saveDir = "";

	if (file.exists())
		loadDir(file.absoluteDir());
	else
		dir = DkSettings::global.lastDir;

	// init cacher
	cacher = 0;
	startStopCacher();
	initFileFilters();
}

/**
 * Default destructor.
 **/ 
DkImageLoader::~DkImageLoader() {

	loaderThread->exit(0);
	loaderThread->wait();
	delete loaderThread;

	if (cacher) {
		cacher->stop();
		qDebug() << "waiting for cacher to stop...";
		cacher->wait();
		delete cacher;
	}

	delete dirWatcher;

	qDebug() << "dir open: " << dir.absolutePath();
	qDebug() << "filepath: " << saveDir.absolutePath();
}



void DkImageLoader::initFileFilters() {

	//// load plugins
	//QDir pluginFolder(QCoreApplication::applicationDirPath());
	//pluginFolder.cd("imageformats");

	//QStringList pluginFilenames = pluginFolder.entryList(QStringList("*.dll"));
	//qDebug() << "searching for plugins: " << pluginFolder.absolutePath();
	//qDebug() << "plugins found: " << pluginFilenames;

	//for (int idx = 0; idx < pluginFilenames.size(); idx++) {
	//	QPluginLoader p(QFileInfo(pluginFolder, pluginFilenames[idx]).absoluteFilePath());
	//	if (!p.load())
	//		qDebug() << "sorry, I could NOT load " << pluginFilenames[idx] << " since:\n" << p.errorString();
	//	
	//}

	if (!openFilters.empty())
		return;


	QList<QByteArray> qtFormats = QImageReader::supportedImageFormats();

	// formats we can save
	if (qtFormats.contains("png"))		saveFilters.append("Portable Network Graphics (*.png)");
	if (qtFormats.contains("jpg"))		saveFilters.append("JPEG (*.jpg *.jpeg)");
	if (qtFormats.contains("j2k"))		saveFilters.append("JPEG 2000 (*.jp2 *.j2k *.jpf *.jpx *.jpm *.jpgx)");
	if (qtFormats.contains("tif"))		saveFilters.append("TIFF (*.tif *.tiff)");
	if (qtFormats.contains("bmp"))		saveFilters.append("Windows Bitmap (*.bmp)");
	if (qtFormats.contains("ppm"))		saveFilters.append("Portable Pixmap (*.ppm)");
	if (qtFormats.contains("xbm"))		saveFilters.append("X11 Bitmap (*.xbm)");
	if (qtFormats.contains("xpm"))		saveFilters.append("X11 Pixmap (*.xpm)");

	// internal filters
#ifdef WITH_WEBP
	saveFilters.append("WebP (*.webp)");
#endif

	// formats we can load
	openFilters += saveFilters;
	if (qtFormats.contains("gif"))		openFilters.append("Graphic Interchange Format (*.gif)");
	if (qtFormats.contains("pbm"))		openFilters.append("Portable Bitmap (*.pbm)");
	if (qtFormats.contains("pgm"))		openFilters.append("Portable Graymap (*.pgm)");
	if (qtFormats.contains("ico"))		openFilters.append("Icon Files (*.ico)");
	if (qtFormats.contains("tga"))		openFilters.append("Targa Image File (*.tga)");
	if (qtFormats.contains("mng"))		openFilters.append("Multi-Image Network Graphics (*.mng)");

#ifdef WITH_OPENCV
	// raw format
	openFilters.append("Nikon Raw (*.nef)");
	openFilters.append("Canon Raw (*.crw *.cr2)");
	openFilters.append("Sony Raw (*.arw)");
	openFilters.append("Digital Negativ (*.dng)");
	openFilters.append("Panasonic Raw (*.rw2)");
	openFilters.append("Minolta Raw (*.mrw)");
#endif

	// stereo formats
	openFilters.append("JPEG Stereo (*.jps)");
	openFilters.append("PNG Stereo (*.pns)");
	openFilters.append("Multi Picture Object (*.mpo)");
	
	// other formats
	openFilters.append("Adobe Photoshop (*.psd)");
	openFilters.append("Large Document Format (*.psb)");
	openFilters.append("Rohkost (*.roh)");

	// load user filters
	QSettings settings;
	openFilters += settings.value("ResourceSettings/userFilters", QStringList()).toStringList();

	for (int idx = 0; idx < openFilters.size(); idx++) {

		QString cFilter = openFilters[idx];
		cFilter = cFilter.section(QRegExp("(\\(|\\))"), 1);
		cFilter = cFilter.replace(")", "");
		DkImageLoader::fileFilters += cFilter.split(" ");
	}

	QString allFilters = DkImageLoader::fileFilters.join(" ");

	// add unknown formats from Qt plugins
	for (int idx = 0; idx < qtFormats.size(); idx++) {

		if (!allFilters.contains(qtFormats.at(idx))) {
			openFilters.append("Image Format (*." + qtFormats.at(idx) + ")");
			DkImageLoader::fileFilters.append("*." + qtFormats.at(idx));
		}
	}

	openFilters.prepend("Image Files (" + fileFilters.join(" ") + ")");

	qDebug() << "supported: " << qtFormats;

#ifdef Q_OS_WIN
	DkImageLoader::fileFilters.append("*.lnk");
#endif
}

/**
 * Clears the path.
 * Calling this method makes the loader forget
 * about the current directory. It also destroys
 * the currently loaded image.
 **/ 
void DkImageLoader::clearPath() {

	QMutexLocker locker(&mutex);
	basicLoader.release();
	
	file.refresh();

	// lastFileLoaded must exist
	if (file.exists())
		lastFileLoaded = file;
	file = QFileInfo();
	editFile = QFileInfo();
	imgMetaData.setFileName(file);	// unload exif too
	//dir = QDir();
}

/**
 * Clears the current file watch.
 **/ 
void DkImageLoader::clearFileWatcher() {

	if (!watcher)
		return;

	if (!watcher->files().isEmpty())
		watcher->removePaths(watcher->files());	// remove all files previously watched
}

/**
 * Loads a given directory and the first image in this directory.
 * @param newDir the directory to be loaded.
 **/ 
bool DkImageLoader::loadDir(QDir newDir, bool scanRecursive) {

	// folder changed signal was emitted
	if (folderUpdated && newDir.absolutePath() == dir.absolutePath()) {

		files = getFilteredFileList(dir, ignoreKeywords, keywords, folderKeywords);		// this line takes seconds if you have lots of files and slow loading (e.g. network)

		// might get empty too (e.g. someone deletes all images
		if (files.empty()) {
			emit updateInfoSignal(tr("%1 \n does not contain any image").arg(dir.absolutePath()), 4000);	// stop showing
			return false;
		}

		//emit updateDirSignal(file, true);		// if the signal is set to true thumbs are updated if images are added to the folder (however this may be nesty)
		emit updateDirSignal(file);
		folderUpdated = false;
		qDebug() << "getting file list.....";
		
		if (cacher)
			cacher->updateDir(files);

	}
	// new folder is loaded
	else if ((newDir.absolutePath() != dir.absolutePath() || files.empty()) && newDir.exists()) {

		// update save directory
		//if (!saveDir.exists()) saveDir = dir;
		dir = newDir;
		dir.setNameFilters(fileFilters);
		dir.setSorting(QDir::LocaleAware);		// TODO: extend
		folderUpdated = false;

		folderKeywords.clear();	// delete key words -> otherwise user may be confused
		emit folderFiltersChanged(folderKeywords);

		if (scanRecursive && DkSettings::global.scanSubFolders) {
			updateSubFolders(dir);
		}
		else 
			files = getFilteredFileList(dir, ignoreKeywords, keywords, folderKeywords);		// this line takes seconds if you have lots of files and slow loading (e.g. network)

		if (files.empty()) {
			emit updateInfoSignal(tr("%1 \n does not contain any image").arg(dir.absolutePath()), 4000);	// stop showing
			return false;
		}

		if (dirWatcher) {
			if (!dirWatcher->directories().isEmpty())
				dirWatcher->removePaths(dirWatcher->directories());
			dirWatcher->addPath(dir.absolutePath());
		}

		if (cacher) {
			
			DkTimer dt;
			
			cacher->setNewDir(dir, files);
			cacher->start();
			qDebug() << "restarting the cacher took me: " << QString::fromStdString(dt.getTotal());
		}

		//qDebug() << "dir watcher: " << dirWatcher->directories();
	}

	return true;
}

void DkImageLoader::startStopCacher() {

	// stop cacher
	if (DkSettings::resources.cacheMemory <= 0 && cacher) {
		cacher->stop();
		cacher->wait();
		delete cacher;
		cacher = 0;
	}

	// start cacher
	if (DkSettings::resources.cacheMemory > 0 && !cacher) {
		cacher = new DkCacher();
		cacher->setNewDir(dir, files);
	}


}

/**
 * Loads the next file.
 * @param silent if true, no status information will be displayed.
 **/ 
void DkImageLoader::nextFile(bool silent) {
	
	qDebug() << "loading next file";
	changeFile(1, silent);
	
}

/**
 * Loads the previous file.
 * @param silent if true, no status information will be displayed.
 **/ 
void DkImageLoader::previousFile(bool silent) {
	
	qDebug() << "loading previous file";
	changeFile(-1, silent);
}

/**
 * Loads the ancesting or subsequent file.
 * @param skipIdx the number of files that should be skipped after/before the current file.
 * @param silent if true, no status information will be displayed.
 **/ 
void DkImageLoader::changeFile(int skipIdx, bool silent, int cacheState) {

	//if (!img.isNull() && !file.exists())
	//	return;
	//if (!file.exists() && !virtualFile.exists()) {
	//	qDebug() << virtualFile.absoluteFilePath() << "does not exist...!!!";
	//	return;
	//}

	// update dir
	if (skipIdx == 0 && cacheState == cache_force_load)
		loadDir(dir, false);
	else
		loadDir(dir);

	mutex.lock();
	QFileInfo loadFile = getChangedFileInfo(skipIdx);
	mutex.unlock();

	// message when reloaded
	if (loadFile.isFile() && loadFile.absoluteFilePath().isEmpty() && skipIdx == 0) {
		QString msg = tr("sorry, %1 does not exist anymore...").arg(virtualFile.fileName());
		if (!silent)
			updateInfoSignal(msg, 4000);
	}


	//if (loadFile.exists())
		load(loadFile, silent, cacheState);
}

/**
 * Loads the ancesting or subsequent thumbnail file.
 * @param skipIdx the number of files that should be skipped after/before the current file.
 * @param silent if true, no status information will be displayed.
 **/ 
QImage DkImageLoader::changeFileFast(int skipIdx, QFileInfo& fileInfo, bool silent) {

	mutex.lock();
	fileInfo = getChangedFileInfo(skipIdx);
	mutex.unlock();

	//if (loadFile.exists())
	// no threading here
	return loadThumb(fileInfo, silent);
}

/**
 * Returns the file info of the ancesting/subsequent file + skipIdx.
 * @param skipIdx the number of files to be skipped from the current file.
 * @param silent if true, no status information will be displayed.
 * @return QFileInfo the file info of the demanded file
 **/ 
QFileInfo DkImageLoader::getChangedFileInfo(int skipIdx, bool silent, bool searchFile) {

	file.refresh();
	virtualFile.refresh();
	bool virtualExists = files.contains(virtualFile.fileName()); // old code here is a bug if the image is e.g. renamed

	qDebug() << "virtual file: " << virtualFile.absoluteFilePath();
	qDebug() << "file: " << file.absoluteFilePath();
	qDebug() << "files: " << files;

	if (!virtualExists && !file.exists())
		return QFileInfo();

	DkTimer dt;

	// load a page (e.g. within a tiff file)
	if (basicLoader.setPageIdx(skipIdx))
		return basicLoader.getFile();

	//if (folderUpdated) {
	//	bool loaded = loadDir((virtualExists) ? virtualFile.absoluteDir() : file.absoluteDir(), false);
	//	if (!loaded)
	//		return QFileInfo();
	//}
	
	if (searchFile && !file.absoluteFilePath().isEmpty()) {
		QDir newDir = (virtualExists && virtualFile.absoluteDir() != dir) ? virtualFile.absoluteDir() : file.absoluteDir();
		qDebug() << "loading new dir: " << newDir.absolutePath();
		qDebug() << "old dir: " << dir.absolutePath();
		bool loaded = loadDir(newDir, false);
		if (!loaded)
			return QFileInfo();
	}

	// locate the current file
	QString cFilename = (virtualExists) ? virtualFile.fileName() : file.fileName();
	int newFileIdx = 0;
	
	if (searchFile) cFileIdx = 0;

	//qDebug() << "virtual file " << virtualFile.absoluteFilePath();
	//qDebug() << "file" << file.absoluteFilePath();

	if (virtualExists || file.exists()) {

		if (searchFile) {
			for ( ; cFileIdx < files.size(); cFileIdx++) {

				if (files[cFileIdx] == cFilename)
					break;
			}
		}
		newFileIdx = cFileIdx + skipIdx;

		// could not locate the file -> it was deleted?!
		if (searchFile && cFileIdx == files.size()) {
			
			// see if the file was deleted
			QStringList filesTmp = files;
			filesTmp.append(cFilename);
			filesTmp = sort(filesTmp, dir);

			cFileIdx = 0;
			
			for ( ; cFileIdx < filesTmp.size(); cFileIdx++) {

				if (filesTmp[cFileIdx] == cFilename)
					break;
			}

			if (filesTmp.size() != cFileIdx) {
				newFileIdx = cFileIdx + skipIdx;
				if (skipIdx > 0) newFileIdx--;	// -1 because the current file does not exist
			}
		}		

		//qDebug() << "subfolders: " << DkSettings::global.scanSubFolders << "subfolder size: " << (subFolders.size() > 1);

		if (DkSettings::global.scanSubFolders && subFolders.size() > 1 && (newFileIdx < 0 || newFileIdx >= files.size())) {

			int folderIdx = 0;

			// locate folder
			for (int idx = 0; idx < subFolders.size(); idx++) {
				if (subFolders[idx] == dir.absolutePath()) {
					folderIdx = idx;
					break;
				}
			}

			if (newFileIdx < 0)
				folderIdx = getPrevFolderIdx(folderIdx);
			else
				folderIdx = getNextFolderIdx(folderIdx);

			qDebug() << "new folder idx: " << folderIdx;
			
			//if (DkSettings::global.loop)
			//	folderIdx %= subFolders.size();

			if (folderIdx >= 0 && folderIdx < subFolders.size()) {
				
				int oldFileSize = files.size();
				loadDir(QDir(subFolders[folderIdx]), false);	// don't scan recursive again
				qDebug() << "loading new folder: " << subFolders[folderIdx];

				if (newFileIdx >= oldFileSize) {
					newFileIdx -= oldFileSize;
					cFileIdx = 0;
					qDebug() << "new skip idx: " << newFileIdx << "cFileIdx: " << cFileIdx << " -----------------------------";
					getChangedFileInfo(newFileIdx, silent, false);
				}
				else if (newFileIdx < 0) {
					newFileIdx += cFileIdx;
					cFileIdx = files.size()-1;
					qDebug() << "new skip idx: " << newFileIdx << "cFileIdx: " << cFileIdx << " -----------------------------";
					getChangedFileInfo(newFileIdx, silent, false);
				}
			}
			//// dir up
			//else if (folderIdx == subFolders.size()) {

			//	qDebug() << "going one up";
			//	dir.cd("..");
			//	loadDir(dir, false);	// don't scan recursive again
			//	newFileIdx += cFileIdx;
			//	cFileIdx = 0;
			//	getChangedFileInfo(newFileIdx, silent, false);
			//}
			//// get root files
			//else if (folderIdx < 0) {
			//	loadDir(dir, false);
			//}

		}

		// this should never happen!
		if (files.empty()) {
			qDebug() << "file list is empty, where it should not be";
			return QFileInfo();
		}

		// loop the directory
		if (DkSettings::global.loop) {
			newFileIdx %= files.size();

			while (newFileIdx < 0)
				newFileIdx = files.size() + newFileIdx;

		}
		// clip to pos1 if skipIdx < -1
		else if (cFileIdx > 0 && newFileIdx < 0) {
			newFileIdx = 0;
		}
		// clip to end if skipIdx > 1
		else if (cFileIdx < files.size()-1 && newFileIdx >= files.size()) {
			newFileIdx = files.size()-1;
		}
		// tell user that there is nothing left to display
		else if (newFileIdx < 0) {
			QString msg = tr("You have reached the beginning");
			if (!silent)
				updateInfoSignal(msg, 1000);
			return QFileInfo();
		}
		// tell user that there is nothing left to display
		else if (newFileIdx >= files.size()) {
			QString msg = tr("You have reached the end");
			
			qDebug() << " you have reached the end ............";

			if (!DkSettings::global.loop)
				emit(setPlayer(false));

			if (!silent)
				updateInfoSignal(msg, 1000);
			return QFileInfo();
		}
	}

	//qDebug() << "file idx changed in: " << QString::fromStdString(dt.getTotal());

	cFileIdx = newFileIdx;

	// file requested becomes current file
	return (files.isEmpty()) ? QFileInfo() : QFileInfo(dir, files[newFileIdx]);
	
}

/**
* Loads the file at index idx.
* @param idx the file index of the file which should be loaded.
**/ 
void DkImageLoader::loadFileAt(int idx) {

	file.refresh();

	if (basicLoader.hasImage() && !file.exists())
		return;

	mutex.lock();

	if (!dir.exists()) {
		QDir newDir = (virtualFile.exists()) ? virtualFile.absoluteDir() : file.absolutePath();	
		loadDir(newDir);
	}

	if (dir.exists()) {

		if (idx == -1) {
			idx = files.size()-1;
		}
		else if (DkSettings::global.loop) {
			idx %= files.size();

			while (idx < 0)
				idx = files.size() + idx;

		}
		else if (idx < 0 && !DkSettings::global.loop) {
			QString msg = tr("You have reached the beginning");
			updateInfoSignal(msg, 1000);
			mutex.unlock();
			return;
		}
		else if (idx >= files.size()) {
			QString msg = tr("You have reached the end");
			if (!DkSettings::global.loop)
				emit(setPlayer(false));
			updateInfoSignal(msg, 1000);
			mutex.unlock();
			return;
		}

	}

	// file requested becomes current file
	QFileInfo loadFile = QFileInfo(dir, files[idx]);
	qDebug() << "[dir] " << loadFile.absoluteFilePath();

	mutex.unlock();
	load(loadFile);

}

/**
 * Returns all files of the current directory.
 * @return QStringList empty list if no directory is set.
 **/ 
QStringList DkImageLoader::getFiles() {

	// guarantee that the file list is up-to-date
	loadDir(dir);
	
	return files;
}

/**
 * Loads the first file of the current directory.
 **/ 
void DkImageLoader::firstFile() {

	loadFileAt(0);
}

/**
 * Loads the last file of the current directory.
 **/ 
void DkImageLoader::lastFile() {
	
	loadFileAt(-1);
}

QImage DkImageLoader::loadThumb(QFileInfo& file, bool silent) {
		
	DkTimer dt;

	if (cacher)
		cacher->pause();	// loadFile re-starts the cacher again

	virtualFile = file;

	// see if we can read the thumbnail from the exif data
	DkMetaData dataExif(file);
	QImage thumb = dataExif.getThumbnail();
	int orientation = dataExif.getOrientation();

	qDebug() << "thumb size: " << thumb.size();

	//// as found at: http://olliwang.com/2010/01/30/creating-thumbnail-images-in-qt/
	//QString filePath = (file.isSymLink()) ? file.symLinkTarget() : file.absoluteFilePath();
			
	if (orientation != -1 && !dataExif.isTiff()) {
		QTransform rotationMatrix;
		rotationMatrix.rotate((double)orientation);
		thumb = thumb.transformed(rotationMatrix);
	}

	if (!thumb.isNull()) {
		file = virtualFile;
		qDebug() << "[thumb] " << file.fileName() << " loaded in: " << QString::fromStdString(dt.getTotal());

		if (file.exists())
			emit updateFileSignal(file, thumb.size());

	}

	return thumb;
}


/**
 * Loads the current file in a thread.
 **/ 
void DkImageLoader::load() {

	load(file);
}

/**
 * Loads the file specified in a thread.
 * @param file the file to be loaded.
 * @param silent if true, no status will be displayed.
 **/ 
void DkImageLoader::load(QFileInfo file, bool silent, int cacheState) {

	// if the locker is in load file we get dead locks if loading is not threaded
	// is it save to lock the mutex before setting up the thread??
	/*QMutexLocker locker(&mutex);*/
	
	// TODO: use QtConcurrent here...
	QMetaObject::invokeMethod(this, "loadFile", Qt::QueuedConnection, Q_ARG(QFileInfo, file), Q_ARG(bool, silent), Q_ARG(int, cacheState));
}

/**
 * Loads the file specified (not threaded!)
 * @param file the file to be loaded.
 * @return bool true if the file could be loaded.
 **/ 
bool DkImageLoader::loadFile(QFileInfo file, bool silent, int cacheState) {
	
	DkTimer dtt;

	QMutexLocker locker(&mutex);

	// null file?
	if (file.fileName().isEmpty()) {
		this->file = lastFileLoaded;
		return false;
	}
	else if (!file.exists()) {
		
		if (!silent) {
			QString msg = tr("Sorry, the file: %1 does not exist... ").arg(file.fileName());
			updateInfoSignal(msg);
		}
		
		fileNotLoadedSignal(file);
		this->file = lastFileLoaded;	// revert to last file

		int fPos = files.indexOf(file.fileName());
		if (fPos >= 0)
			files.removeAt(fPos);

		return false;
	}
	else if (!file.permission(QFile::ReadUser)) {
		
		if (!silent) {
			QString msg = tr("Sorry, you are not allowed to read: %1").arg(file.fileName());
			updateInfoSignal(msg);
		}
		
		fileNotLoadedSignal(file);
		this->file = lastFileLoaded;	// revert to last file

		return false;
	}

	if (cacher)
		cacher->pause();

	DkTimer dt;

	//test exif
	//DkExif dataExif = DkExif(file);
	//int orientation = dataExif.getOrientation();
	//dataExif.getExifKeys();
	//dataExif.getExifValues();
	//dataExif.getIptcKeys();
	//dataExif.getIptcValues();
	//dataExif.saveOrientation(90);

	if (!silent)
		emit updateSpinnerSignalDelayed(true);

	qDebug() << "loading: " << file.absoluteFilePath();

	bool imgLoaded = false;
	bool imgRotated = false;
	
	DkTimer dtc;

	// critical section -> threads
	if (cacher && cacheState != cache_force_load && !basicLoader.isDirty()) {
		
		QVector<DkImageCache> cache = cacher->getCache();
		//QMutableVectorIterator<DkImageCache> cIter(cacher->getCache());

		for (int idx = 0; idx < cache.size(); idx++) {
			
			//cIter.next();

			if (cache.at(idx).getFile() == file) {

				if (cache.at(idx).getCacheState() == DkImageCache::cache_loaded) {
					DkImageCache cCache = cache.at(idx);
					QImage tmp = cCache.getImage();
					basicLoader.setImage(tmp, file);
					imgLoaded = basicLoader.hasImage();

					// nothing todo here
					if (!imgLoaded)
						break;

					imgRotated = cCache.isRotated();
				}
				break; 
			}
			//cIter.next();
		}
	}

	qDebug() << "loading from cache takes: " << QString::fromStdString(dtc.getTotal());
	
	if (!imgLoaded) {
		try {
			imgLoaded = basicLoader.loadGeneral(file);
		} catch(...) {
			imgLoaded = false;
		}
	}

	this->virtualFile = file;

	if (!silent)
		emit updateSpinnerSignalDelayed(false);	// stop showing

	qDebug() << "image loaded in: " << QString::fromStdString(dt.getTotal());
	
	if (imgLoaded) {
		
		DkMetaData imgMetaData(file);		
		int orientation = imgMetaData.getOrientation();

		//QStringList keys = imgMetaData.getExifKeys();
		//qDebug() << keys;

		if (!imgMetaData.isTiff() && !imgRotated && !DkSettings::metaData.ignoreExifOrientation)
			basicLoader.rotate(orientation);
		
		if (cacher && cacheState != cache_disable_update) 
			cacher->setCurrentFile(file, basicLoader.image());

		qDebug() << "orientation set in: " << QString::fromStdString(dt.getIvl());
			
		// update watcher
		emit updateFileWatcherSignal(file);		
		this->file = file;
		lastFileLoaded = file;
		editFile = QFileInfo();
		loadDir(file.absoluteDir(), false);
		
		emit updateImageSignal();
		emit updateDirSignal(file);	// this should call updateFileSignal too
		sendFileSignal();

		// update history
		updateHistory();
	}
	else {
		//if (!silent) {
			QString msg = tr("Sorry, I could not load: %1").arg(file.fileName());
			updateInfoSignal(msg);
			this->file = lastFileLoaded;	// revert to last file
			qDebug() << "reverting to: " << lastFileLoaded.fileName();
			loadDir(this->file.absoluteDir(), false);
		//}
		fileNotLoadedSignal(file);
		
		startStopCacher();
		if (cacher) {
			cacher->start();
			cacher->play();
		}

		qDebug() << "I did load it silent: " << silent;
		return false;
	}

	startStopCacher();
	if (cacher) {
		cacher->start();
		cacher->play();
	}

	qDebug() << "total loading time: " << QString::fromStdString(dtt.getTotal());

	return true;
}


/**
 * Saves the file specified in a thread.
 * If the file already exists, it will be replaced.
 * @param file the (new) filename.
 * @param fileFilter the file extenstion (e.g. *.jpg)
 * @param saveImg the image to be saved
 * @param compression the compression method (for *.jpg or *.tif images)
 **/ 
void DkImageLoader::saveFile(QFileInfo file, QString fileFilter, QImage saveImg, int compression) {

		QMetaObject::invokeMethod(this, "saveFileIntern", 
			Qt::QueuedConnection, 
			Q_ARG(QFileInfo, file), 
			Q_ARG(QString, fileFilter), 
			Q_ARG(QImage, saveImg),
			Q_ARG(int, compression));
}

/**
 * Saves a file in a thread with no status information.
 * @param file the file name/path
 * @param img the image to be saved
 **/ 
void DkImageLoader::saveFileSilentThreaded(QFileInfo file, QImage img) {

	QMetaObject::invokeMethod(this, "saveFileSilentIntern", Qt::QueuedConnection, Q_ARG(QFileInfo, file), Q_ARG(QImage, img));
}

/**
 * Saves a temporary file to the folder specified in Settings.
 * @param img the image (which was in most cases pasted to nomacs)
 **/ 
QFileInfo DkImageLoader::saveTempFile(QImage img, QString name, QString fileExt, bool force, bool threaded) {

	QFileInfo tmpPath = QFileInfo(DkSettings::global.tmpPath + "\\");
	
	if (!force && (!DkSettings::global.useTmpPath || !tmpPath.exists())) {
		qDebug() << tmpPath.absolutePath() << "does not exist";
		return QFileInfo();
	}
	else if ((!DkSettings::global.useTmpPath || !tmpPath.exists())) {

#ifdef WIN32
		
		// TODO: this path seems to be perfectly ok (you can copy it to windows explorer) - however Qt thinks it does not exist??
		QString defaultPath = getenv("HOMEPATH");
		defaultPath = "C:" + defaultPath + "\\My Pictures\\";
		tmpPath = defaultPath;

		qDebug() << "default path: " << tmpPath.absoluteFilePath();
#endif

		if (!tmpPath.isDir()) {
			// load system default open dialog
			QString dirName = QFileDialog::getExistingDirectory(DkNoMacs::getDialogParent(), tr("Save Directory"),
				getDir().absolutePath());

			tmpPath = dirName + "/";

			if (!tmpPath.exists())
				return QFileInfo();
		}
	}

	qDebug() << "tmpPath: " << tmpPath.absolutePath();
	
	//// TODO: call save file silent threaded...
	//for (int idx = 1; idx < 10000; idx++) {
	
		QString fileName = name + "-" + QDateTime::currentDateTime().toString("yyyy-MM-dd hh.mm.ss") + fileExt;

		//if (idx < 10)
		//	fileName += "000";
		//else if (idx < 100)
		//	fileName += "00";
		//else if (idx < 1000)
		//	fileName += "0";
		//
		//fileName += QString::number(idx) + fileExt;

		QFileInfo tmpFile = QFileInfo(tmpPath.absolutePath(), fileName);

		if (!tmpFile.exists()) {
			
			if (threaded)
				saveFileSilentThreaded(tmpFile, img);
			else
				saveFileSilentIntern(tmpFile, img);
			
			qDebug() << tmpFile.absoluteFilePath() << "saved...";

			return tmpFile;
		}
	//}

	return QFileInfo();
}

/**
 * Saves a file (not threaded!)
 * If the file already exists, it will be replaced.
 * @param file the file name/path
 * @param fileFilter the file extension (e.g. *.jpg)
 * @param saveImg the image to be saved
 * @param compression the compression method (for jpg, tif)
 **/ 
void DkImageLoader::saveFileIntern(QFileInfo file, QString fileFilter, QImage saveImg, int compression) {
	
	QMutexLocker locker(&mutex);
	
	if (basicLoader.hasImage() && saveImg.isNull()) {
		QString msg = tr("I can't save an empty file, sorry...\n");
		emit newErrorDialog(msg);
		return;
	}
	if (!file.absoluteDir().exists()) {
		QString msg = tr("Sorry, the directory: %1  does not exist\n").arg(file.absolutePath());
		emit newErrorDialog(msg);
		return;
	}
	if (file.exists() && !file.isWritable()) {
		QString msg = tr("Sorry, I can't write to the file: %1").arg(file.fileName());
		emit newErrorDialog(msg);
		return;
	}

	QString filePath = file.absoluteFilePath();

	// if the user did not specify the suffix - append the suffix of the file filter
	QString newSuffix = file.suffix();
	if (newSuffix == "") {
		
		newSuffix = fileFilter.remove(0, fileFilter.indexOf("."));
		printf("new suffix: %s\n", newSuffix.toStdString().c_str());

		int endSuffix = -1;
		if (newSuffix.indexOf(")") == -1)
			endSuffix =  newSuffix.indexOf(" ");
		else if (newSuffix.indexOf(" ") == -1)
			endSuffix =  newSuffix.indexOf(")");
		else
			endSuffix = qMin(newSuffix.indexOf(")"), newSuffix.indexOf(" "));

		filePath.append(newSuffix.left(endSuffix));
	}
	
	// update watcher
	if (this->file.exists() && watcher)
		watcher->removePath(this->file.absoluteFilePath());
	
	QImage sImg = (saveImg.isNull()) ? basicLoader.image() : saveImg;
		
	emit updateInfoSignalDelayed(tr("saving..."), true);
	bool saved = basicLoader.save(filePath, sImg, compression);
	emit updateInfoSignalDelayed(tr("saving..."), false);

	if (QFileInfo(filePath).exists())
		qDebug() << QFileInfo(filePath).absoluteFilePath() << " (before exif) exists...";
	else
		qDebug() << QFileInfo(filePath).absoluteFilePath() << " (before exif) does NOT exists...";

	if (saved) {
		
		try {
			// TODO: remove path?!
			imgMetaData.saveThumbnail(DkThumbsLoader::createThumb(sImg), QFileInfo(filePath));
			imgMetaData.saveMetaDataToFile(QFileInfo(filePath)/*, dataExif.getOrientation()*/);
		} catch (DkException de) {
			// do nothing -> the file type does not support meta data
		}
		catch (...) {

			if (!restoreFile(QFileInfo(filePath)))
				emit newErrorDialog("sorry, I destroyed: " + QFileInfo(filePath).fileName() + "\n remove the numbers after the file extension in order to restore the file...");
			qDebug() << "could not copy meta-data to file" << filePath;
		}

		// assign the new save directory
		saveDir = QDir(file.absoluteDir());
		DkSettings::global.lastSaveDir = file.absolutePath();	// we currently don't use that
				
		// reload my dir (if it was changed...)
		this->file = QFileInfo(filePath);

		if (this->file.exists())
			qDebug() << this->file.absoluteFilePath() << " (refreshed) exists...";
		else
			qDebug() << this->file.absoluteFilePath() << " (refreshed) does NOT exist...";

		this->editFile = QFileInfo();
		this->virtualFile = this->file;
		basicLoader.setImage(sImg, this->file);
		loadDir(file.absoluteDir());
		if (cacher) cacher->setCurrentFile(file, basicLoader.image());

		emit updateImageSignal();
		sendFileSignal();
				
		printf("I could save the image...\n");
	}
	else {
		QString msg = tr("Sorry, I can't save: %1").arg(file.fileName());
		emit newErrorDialog(msg);
	}

	emit updateFileWatcherSignal(this->file);

}

/**
 * Saves the file (not threaded!).
 * No status information will be displayed if this function is called.
 * @param file the file name/path.
 * @param saveImg the image to be saved.
 **/ 
void DkImageLoader::saveFileSilentIntern(QFileInfo file, QImage saveImg) {

	QMutexLocker locker(&mutex);
	
	this->file.refresh();

	// update watcher
	if (this->file.exists() && watcher)
		watcher->removePath(this->file.absoluteFilePath());
	
	emit updateInfoSignalDelayed(tr("saving..."), true);
	QString filePath = file.absoluteFilePath();
	QImage sImg = (saveImg.isNull()) ? basicLoader.image() : saveImg; 
	bool saved = basicLoader.save(filePath, sImg);
	emit updateInfoSignalDelayed(tr("saving..."), false);	// stop the label
	
	if (saved)
		emit updateFileWatcherSignal(file);
	else 
		emit updateFileWatcherSignal(this->file);

	if (!saveImg.isNull() && saved) {
		
		if (this->file.exists()) {
			try {
				// TODO: remove watcher path?!
				imgMetaData.saveThumbnail(DkThumbsLoader::createThumb(sImg), QFileInfo(filePath));
				imgMetaData.saveMetaDataToFile(QFileInfo(filePath));
			} catch (DkException e) {

				qDebug() << "can't write metadata...";
			} catch (...) {
				
				if (!restoreFile(QFileInfo(filePath)))
					emit newErrorDialog("sorry, I destroyed: " + QFileInfo(filePath).fileName() + "\n remove the numbers after the file extension in order to restore the file...");
			}
		}

		// reload my dir (if it was changed...)
		this->file = QFileInfo(filePath);
		this->editFile = QFileInfo();

		if (saved)
			load(filePath, true);

		//this->virtualFile = this->file;
		//basicLoader.setImage(saveImg, this->file);
		//loadDir(this->file.absoluteDir());

		//if (cacher) cacher->setCurrentFile(file, basicLoader.image());
		//sendFileSignal();
	}
}

/**
 * Saves the rating to the metadata.
 * This function does nothing if an image format
 * is loaded that does not support metadata.
 * @param rating the rating.
 **/ 
void DkImageLoader::saveRating(int rating) {

	file.refresh();

	// file might be edited
	if (!file.exists())
		return;

	QMutexLocker locker(&mutex);
	// update watcher
	if (this->file.exists() && watcher)
		watcher->removePath(this->file.absoluteFilePath());

	try {
		
		imgMetaData.saveRating(rating);
	}catch(...) {
		
		if (!restoreFile(this->file))
			emit updateInfoSignal(tr("Sorry, I could not restore: %1").arg(file.fileName()));
	}
	emit updateFileWatcher(this->file);

}

//void DkImageLoader::enableWatcher(bool enable) {
//	
//	watcherEnabled = enable;
//}

/**
 * Updates the file history.
 * The file history stores the last 10 folders.
 **/ 
void DkImageLoader::updateHistory() {

	DkSettings::global.lastDir = file.absolutePath();

	DkSettings::global.recentFiles.removeAll(file.absoluteFilePath());
	DkSettings::global.recentFolders.removeAll(file.absolutePath());

	DkSettings::global.recentFiles.push_front(file.absoluteFilePath());
	DkSettings::global.recentFolders.push_front(file.absolutePath());

	DkSettings::global.recentFiles.removeDuplicates();
	DkSettings::global.recentFolders.removeDuplicates();

	for (int idx = 0; idx < DkSettings::global.recentFiles.size()-DkSettings::global.numFiles-10; idx++)
		DkSettings::global.recentFiles.pop_back();

	for (int idx = 0; idx < DkSettings::global.recentFolders.size()-DkSettings::global.numFiles-10; idx++)
		DkSettings::global.recentFolders.pop_back();


	// TODO: shouldn't we delete that -> it's saved when nomacs is closed anyway
	DkSettings s = DkSettings();
	s.save();
}

// image manipulation --------------------------------------------------------------------
/**
 * Deletes the currently loaded file.
 **/ 
void DkImageLoader::deleteFile() {
	
	file.refresh();

	if (file.exists()) {

		QFile* fileHandle = new QFile(file.absoluteFilePath());

		//if (fileHandle->permissions() != QFile::WriteUser) {		// on unix this may lead to troubles (see qt doc)
		//	emit updateInfoSignal("You don't have permissions to delete: \n" % file.fileName());
		//	return;
		//}

		QFileInfo fileToDelete = file;

		// load the next file
		QFileInfo loadFile = getChangedFileInfo(1, true);

		if (loadFile.exists())
			load(loadFile, true);
		else {
			clearPath();
			emit updateImageSignal();
		}
		
		if (fileHandle->remove())
			emit updateInfoSignal(tr("%1 deleted...").arg(fileToDelete.fileName()));
		else
			emit updateInfoSignal(tr("Sorry, I could not delete: %1").arg(fileToDelete.fileName()));
	}

}

/**
 * Rotates the image.
 * First, we try to set the rotation flag in the metadata
 * (this is the fastest way to rotate an image).
 * If this does not work, the image matrix is rotated.
 * @param angle the rotation angle in degree.
 **/ 
void DkImageLoader::rotateImage(double angle) {

	qDebug() << "rotating image...";
	file.refresh();

	if (!basicLoader.hasImage()) {
		qDebug() << "sorry, loader has no image";
		return;
	}

	if (file.exists() && watcher) {
		mutex.lock();
		watcher->removePath(this->file.absoluteFilePath());
		mutex.unlock();
	}
	//updateInfoSignal("test", 5000);

	try {
		
		mutex.lock();
		basicLoader.rotate(angle);
		mutex.unlock();

		emit updateImageSignal();
		QCoreApplication::sendPostedEvents();	// update immediately as we interlock otherwise

		mutex.lock();
		if (file.exists() && DkSettings::metaData.saveExifOrientation) {
			
			imgMetaData.saveOrientation((int)angle);

			QImage thumb = DkThumbsLoader::createThumb(basicLoader.image());
			if (imgMetaData.isJpg()) {
				// undo exif orientation
				DkBasicLoader loader;
				loader.setImage(thumb, QFileInfo());
				loader.rotate(-imgMetaData.getOrientation());
				thumb = loader.image();
			}
			imgMetaData.saveThumbnail(thumb, file);
		}
		else if (file.exists() && !DkSettings::metaData.saveExifOrientation) {
			qDebug() << "file: " << file.fileName() << " exists...";
			imgMetaData.saveOrientation(0);		// either metadata throws or we force throwing
			throw DkException("User forces NO exif orientation", __LINE__, __FILE__);
		}
		

		mutex.unlock();

		sendFileSignal();
	}
	catch(DkException de) {

		mutex.unlock();

		// TODO: saveFileSilentThreaded is in the main thread (find out why)
		// TODO: in this case the image is reloaded (file watcher seems to be active)
		// make a silent save -> if the image is just cached, do not save it
		if (file.exists())
			saveFileSilentThreaded(file);
	}
	catch(...) {	// if file is locked... or permission is missing
		mutex.unlock();

		// try restoring the file
		if (!restoreFile(file))
			emit updateInfoSignal(tr("Sorry, I could not restore: %1").arg(file.fileName()));
	}

	if (cacher)
		cacher->setCurrentFile(file, basicLoader.image());

	emit updateFileWatcherSignal(this->file);

}

/**
 * Restores files that were destroyed by the Exiv2 lib.
 * If a watch (or some other read lock) is on a file, the
 * Exiv2 lib is known do damage the files on Windows.
 * This function restores these files.
 * @param fileInfo the file to be restored.
 * @return bool true if the file could be restored.
 **/ 
bool DkImageLoader::restoreFile(const QFileInfo& fileInfo) {

	QStringList files = fileInfo.dir().entryList();
	QString fileName = fileInfo.fileName();
	QRegExp filePattern(fileName + "[0-9]+");
	QString backupFileName;

	// if exif crashed it saved a backup file with the format: filename.png1232
	for (int idx = 0; idx < files.size(); idx++) {

		if (filePattern.exactMatch(files[idx])) {
			backupFileName = files[idx];
			break;
		}
	}

	if (backupFileName.isEmpty()) {
		qDebug() << "I could not locate the backup file...";
		return true;
	}

	// delete the destroyed file
	QFile file(fileInfo.absoluteFilePath());
	QFile backupFile(fileInfo.absolutePath() + QDir::separator() + backupFileName);

	if (file.size() == 0 || file.size() <= backupFile.size()) {
		
		if (!file.remove()) {

			// ok I did not destroy the original file - so delete the back-up
			// -> this reverts the file - but otherwise we spam to the disk 
			// actions reverted here include meta data saving
			if (file.size() != 0)
				return backupFile.remove();

			qDebug() << "I could not remove the file...";
			return false;
		}
	}
	else {
		
		qDebug() << "non-empty file: " << fileName << " I won't delete it...";
		qDebug() << "file size: " << file.size() << " back-up file size: " << backupFile.size();
		return false;
	}

	// now 
	return backupFile.rename(fileInfo.absoluteFilePath());
}

void DkImageLoader::updateFileWatcher(QFileInfo filePath) {

	//if (watcher)
	//	delete watcher;

	//watcher = new QFileSystemWatcher(QStringList(filePath.absoluteFilePath()), this);
	//connect(watcher, SIGNAL(fileChanged(QString)), this, SLOT(fileChanged(QString)));

	//qDebug() << "file watcher updated: " << filePath.absoluteFilePath();
}

void DkImageLoader::disableFileWatcher() {

	//if (watcher) {
	//	delete watcher;
	//	watcher = 0;
	//}

}

/**
 * Reloads the currently loaded file if it was edited by another software.
 * @param path the file path of the changed file.
 **/ 
void DkImageLoader::fileChanged(const QString& path) {

	qDebug() << "file updated: " << path;

	// ignore if watcher was disabled
	if (path == file.absoluteFilePath()) {
		QMutexLocker locker(&mutex);
		load(QFileInfo(path), true, cache_force_load);
	}
	else
		qDebug() << "file watcher is not up-to-date...";
}

/**
 * Reloads the file index if the directory was edited.
 * @param path the path to the current directory
 **/ 
void DkImageLoader::directoryChanged(const QString& path) {

	if (path.isEmpty() || QDir(path) == dir.absolutePath()) {

		folderUpdated = true;
		
		// guarantee, that only every XX seconds a folder update occurs
		// think of a folder where 100s of files are written to...
		// as this could be pretty fast, the thumbsloader (& whoever) would create a 
		// greater offset and slow down the system
		if ((path.isEmpty() && timerBlockedUpdate) || (!path.isEmpty() && !delayedUpdateTimer.isActive())) {

			emit updateDirSignal(file, DkThumbsLoader::dir_updated);			
			timerBlockedUpdate = false;

			if (!path.isEmpty())
				delayedUpdateTimer.start(3000);
		}
		else
			timerBlockedUpdate = true;
	}
	
}

bool DkImageLoader::isCached(QFileInfo& file) {

	if (!cacher)
		return false;

	QVectorIterator<DkImageCache> cIter(cacher->getCache());

	while (cIter.hasNext()) {
		const DkImageCache& cCache = cIter.next();

		if (cCache.getFile() == file) {

			if (cCache.getCacheState() == DkImageCache::cache_loaded) {
				return true;
			}
		}
	}

	return false;
}

/**
 * Returns true if a file was specified.
 * @return bool true if a file name/path was specified
 **/ 
bool DkImageLoader::hasFile() {

	return file.exists() | editFile.exists();
}

bool DkImageLoader::hasMovie() {

	QString newSuffix = file.suffix();
	return file.exists() && newSuffix.contains(QRegExp("(gif|mng)", Qt::CaseInsensitive));

}

/**
 * Returns the currently loaded file information.
 * @return QFileInfo the current file info
 **/ 
QFileInfo DkImageLoader::getFile() {

	// don't need locker here - const ?!
	QMutexLocker locker(&mutex);
	return (file.exists()) ? file : editFile;
}

/**
 * Returns the currently loaded directory.
 * @return QDir the currently loaded directory.
 **/ 
QDir DkImageLoader::getDir() {

	QMutexLocker locker(&mutex);
	return dir;
}

QStringList DkImageLoader::getFoldersRecursive(QDir dir) {

	//DkTimer dt;
	QStringList subFolders;
	//qDebug() << "scanning recursively: " << dir.absolutePath();

	if (DkSettings::global.scanSubFolders) {

		QDirIterator dirs(dir.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
	
		int nFolders = 0;
		while (dirs.hasNext()) {
			dirs.next();
			subFolders << dirs.filePath();
			nFolders++;

			if (nFolders > 100)
				break;
			
			//getFoldersRecursive(dirs.filePath(), subFolders);
			//qDebug() << "loop: " << dirs.filePath();
		}
	}	

	subFolders << dir.absolutePath();

	qSort(subFolders.begin(), subFolders.end(), compLogicQString);
	

	qDebug() << dir.absolutePath();
	
	//qDebug() << "scanning folders recursively took me: " << QString::fromStdString(dt.getTotal());
	return subFolders;
}

void DkImageLoader::updateSubFolders(QDir rootDir) {
	
	subFolders = getFoldersRecursive(rootDir);

	qDebug() << subFolders;

	// find the first subfolder that has images
	for (int idx = 0; idx < subFolders.size(); idx++) {
		dir = subFolders[idx];
		files = getFilteredFileList(dir, ignoreKeywords, keywords);		// this line takes seconds if you have lots of files and slow loading (e.g. network)
		if (!files.empty())
			break;
	}

}

int DkImageLoader::getNextFolderIdx(int folderIdx) {
	
	int nextIdx = -1;

	if (subFolders.empty())
		return nextIdx;

	// find the first sub folder that has images
	for (int idx = 1; idx < subFolders.size(); idx++) {
		
		int tmpNextIdx = folderIdx + idx;

		if (DkSettings::global.loop)
			tmpNextIdx %= subFolders.size();
		else if (tmpNextIdx >= subFolders.size())
			return -1;

		QDir cDir = subFolders[tmpNextIdx];
		QStringList cFiles = getFilteredFileList(cDir, ignoreKeywords, keywords);		// this line takes seconds if you have lots of files and slow loading (e.g. network)
		if (!cFiles.empty()) {
			nextIdx = tmpNextIdx;
			break;
		}
	}

	return nextIdx;
}

int DkImageLoader::getPrevFolderIdx(int folderIdx) {
	
	int prevIdx = -1;

	if (subFolders.empty())
		return prevIdx;

	// find the first sub folder that has images
	for (int idx = 1; idx < subFolders.size(); idx++) {

		int tmpPrevIdx = folderIdx - idx;

		if (DkSettings::global.loop && tmpPrevIdx < 0)
			tmpPrevIdx += subFolders.size();
		else if (tmpPrevIdx < 0)
			return -1;

		QDir cDir = subFolders[tmpPrevIdx];
		QStringList cFiles = getFilteredFileList(cDir, ignoreKeywords, keywords);		// this line takes seconds if you have lots of files and slow loading (e.g. network)
		if (!cFiles.empty()) {
			prevIdx = tmpPrevIdx;
			break;
		}
	}

	return prevIdx;
}

/**
 * Returns the file list of the directory dir.
 * Note: this function might get slow if lots of files (> 10000) are in the
 * directory or if the directory is in the net.
 * Currently the file list is sorted according to the system specification.
 * @param dir the directory to load the file list from.
 * @param ignoreKeywords if one of these keywords is in the file name, the file will be ignored.
 * @param keywords if one of these keywords is not in the file name, the file will be ignored.
 * @return QStringList all filtered files of the current directory.
 **/ 
QStringList DkImageLoader::getFilteredFileList(QDir dir, QStringList ignoreKeywords, QStringList keywords, QStringList folderKeywords) {

	DkTimer dt;

#ifdef WIN32

	QString winPath = QDir::toNativeSeparators(dir.path()) + "\\*.*";

	wchar_t* fnameT = L"C:\\VSProjects\\img\\*.*";
	const wchar_t* fname = reinterpret_cast<const wchar_t *>(winPath.utf16());

	WIN32_FIND_DATAW findFileData;
	HANDLE MyHandle = FindFirstFileW(fname, &findFileData);

	std::vector<std::wstring> fileNameList;
	std::wstring fileName;

	if( MyHandle != INVALID_HANDLE_VALUE) {
		
		do {

			fileName = findFileData.cFileName;
			fileNameList.push_back(fileName);	// TODO: sort correct according to numbers
		} while(FindNextFileW(MyHandle, &findFileData) != 0);
	}

	FindClose(MyHandle);
	
	// slow regexp
	//QString extPattern = ".+((\\" + fileFilters.join("$)|(\\") + "$))";
	//extPattern.replace("*", "");
	//QRegExp exp(extPattern, Qt::CaseInsensitive);

	// remove the * in fileFilters
	QStringList fileFiltersClean = fileFilters;
	for (int idx = 0; idx < fileFilters.size(); idx++)
		fileFiltersClean[idx].replace("*", "");

	//std::sort(fileNameList.begin(), fileNameList.end(), wCompLogic);

	QStringList fileList;
	std::vector<std::wstring>::iterator lIter = fileNameList.begin();

	// convert to QStringList
	for (unsigned int idx = 0; idx < fileNameList.size(); idx++, lIter++) {
		
		QString qFilename = DkUtils::stdWStringToQString(*lIter);

		// believe it or not, but this is 10 times faster than QRegExp
		// drawback: we also get files that contain *.jpg*
		for (int idx = 0; idx < fileFiltersClean.size(); idx++) {

			if (qFilename.contains(fileFiltersClean[idx], Qt::CaseInsensitive)) {
				fileList.append(qFilename);
				break;
			}
		}
	}

	qDebug() << "WinAPI, indexed (" << fileList.size() <<") files in: " << QString::fromStdString(dt.getTotal());
#else

	// true file list
	dir.setSorting(QDir::LocaleAware);
	QStringList fileList = dir.entryList(fileFilters);
	qDebug() << "Qt, sorted file list computed in: " << QString::fromStdString(dt.getIvl());
	qDebug() << fileList;

#endif

	for (int idx = 0; idx < ignoreKeywords.size(); idx++) {
		QRegExp exp = QRegExp("^((?!" + ignoreKeywords[idx] + ").)*$");
		exp.setCaseSensitivity(Qt::CaseInsensitive);
		fileList = fileList.filter(exp);
	}

	for (int idx = 0; idx < keywords.size(); idx++) {
		fileList = fileList.filter(keywords[idx], Qt::CaseInsensitive);
	}

	if (!folderKeywords.empty()) {
		
		QStringList resultList = fileList;
		for (int idx = 0; idx < folderKeywords.size(); idx++) {
			resultList = resultList.filter(folderKeywords[idx], Qt::CaseInsensitive);
		}

		// if string match returns nothing -> try a regexp
		if (resultList.empty())
			resultList = fileList.filter(QRegExp(folderKeywords.join(" ")));

		qDebug() << "filtered file list (get)" << resultList;
		qDebug() << "keywords: " << folderKeywords;
		fileList = resultList;
	}

	if (DkSettings::resources.filterDuplicats) {

		QString preferredExtension = DkSettings::resources.preferredExtension;
		preferredExtension = preferredExtension.replace("*.", "");
		qDebug() << "preferred extension: " << preferredExtension;

		QStringList resultList = fileList;
		fileList.clear();
		
		for (int idx = 0; idx < resultList.size(); idx++) {
			
			QFileInfo cFName = QFileInfo(resultList.at(idx));

			if (preferredExtension.compare(cFName.suffix(), Qt::CaseInsensitive) == 0) {
				fileList.append(resultList.at(idx));
				continue;
			}

			QString cFBase = cFName.baseName();
			bool remove = false;

			for (int cIdx = 0; cIdx < resultList.size(); cIdx++) {

				QString ccBase = QFileInfo(resultList.at(cIdx)).baseName();

				if (cIdx != idx && ccBase == cFBase && resultList.at(cIdx).contains(preferredExtension, Qt::CaseInsensitive)) {
					remove = true;
					break;
				}
			}
			
			if (!remove)
				fileList.append(resultList.at(idx));
		}
	}

	fileList = sort(fileList, dir);

	return fileList;
}

QStringList DkImageLoader::sort(const QStringList& files, const QDir& dir) {

	QFileInfoList fList;

	for (int idx = 0; idx < files.size(); idx++)
		fList.append(QFileInfo(dir, files.at(idx)));

	switch(DkSettings::global.sortMode) {

	case DkSettings::sort_filename:
		
		if (DkSettings::global.sortDir == DkSettings::sort_ascending)
			qSort(fList.begin(), fList.end(), compFilename);
		else
			qSort(fList.begin(), fList.end(), compFilenameInv);
		break;

	case DkSettings::sort_date_created:
		if (DkSettings::global.sortDir == DkSettings::sort_ascending) {
			qSort(fList.begin(), fList.end(), compDateCreated);
			qSort(fList.begin(), fList.end(), compDateCreated);		// sort twice -> in order to guarantee that same entries are sorted correctly (thumbsloader)
		}
		else { 
			qSort(fList.begin(), fList.end(), compDateCreatedInv);
			qSort(fList.begin(), fList.end(), compDateCreatedInv);
		}
		break;

	case DkSettings::sort_date_modified:
		if (DkSettings::global.sortDir == DkSettings::sort_ascending) {
			qSort(fList.begin(), fList.end(), compDateModified);
			qSort(fList.begin(), fList.end(), compDateModified);
		}
		else {
			qSort(fList.begin(), fList.end(), compDateModifiedInv);
			qSort(fList.begin(), fList.end(), compDateModifiedInv);
		}
		break;
	case DkSettings::sort_random:
			qSort(fList.begin(), fList.end(), compRandom);
		break;

	default:
		// filename
		qSort(fList.begin(), fList.end(), compFilename);

	}

	QStringList sFiles;
	for (int idx = 0; idx < fList.size(); idx++)
		sFiles.append(fList.at(idx).fileName());

	return sFiles;
}

void DkImageLoader::sort() {

	files = sort(files, dir);
	emit updateDirSignal(file, DkThumbsLoader::dir_updated);
}


/**
 * Returns the directory where files are saved to.
 * @return QDir the directory where the user saved the last file to.
 **/ 
QDir DkImageLoader::getSaveDir() {

	qDebug() << "save dir: " << dir;

	if (!saveDir.exists())
		return dir;
	else
		return saveDir;
}

/**
 * Returns the file extension of the current file.
 * @return QString current file extension.
 **/ 
QString DkImageLoader::getCurrentFilter() {

	QString cSuffix = file.suffix();

	for (int idx = 0; idx < saveFilters.size(); idx++) {

		if (saveFilters[idx].contains(cSuffix))
			return saveFilters[idx];
	}

	return "";
}

/**
 * Returns if a file is supported by nomacs or not.
 * Note: this function only checks for a valid extension.
 * @param fileInfo the file info of the file to be validated.
 * @return bool true if the file format is supported.
 **/ 
bool DkImageLoader::isValid(const QFileInfo& fileInfo) {

	printf("accepting file...\n");

	QFileInfo fInfo = fileInfo;
	if (fInfo.isSymLink())
		fInfo = fileInfo.symLinkTarget();

	if (!fInfo.exists())
		return false;

	QString fileName = fInfo.fileName();
	qDebug() << "filename: " << fileName;
	
	for (int idx = 0; idx < fileFilters.size(); idx++) {

		QRegExp exp = QRegExp(fileFilters.at(idx), Qt::CaseInsensitive);
		exp.setPatternSyntax(QRegExp::Wildcard);
		if (exp.exactMatch(fileName))
			return true;
	}

	printf("I did not accept... honestly...\n");

	return false;

}

///**
// * @deprecated
// * This 
// * @param fileInfo
// * @param dir
// * @return int
// **/ 
//int DkImageLoader::locateFile(QFileInfo& fileInfo, QDir* dir) {
//
//	if (!fileInfo.exists())
//		return -1;
//
//	bool newDir = (dir) ? false : true;
//
//	if (!dir) {
//		dir = new QDir(fileInfo.absoluteDir());
//		dir->setNameFilters(fileFilters);
//		dir->setSorting(QDir::LocaleAware);
//	}
//
//	// locate the current file
//	QStringList files = dir->entryList(fileFilters);
//	QString cFilename = fileInfo.fileName();
//
//	int fileIdx = 0;
//	for ( ; fileIdx < files.size(); fileIdx++) {
//
//		if (files[fileIdx] == cFilename)
//			break;
//	}
//
//	if (fileIdx == files.size()) fileIdx = -1;
//
//	if (newDir)
//		delete dir;
//
//	return fileIdx;
//}

void DkImageLoader::loadLastDir() {

	if (DkSettings::global.recentFolders.empty())
		return;

	QDir lastDir = DkSettings::global.recentFolders[0];
	setDir(lastDir);
}

void DkImageLoader::updateCacheIndex() {
	
	if (cacher)
		cacher->setCurrentFile(file, basicLoader.image());
}

void DkImageLoader::setFolderFilters(QStringList filters) {

	folderKeywords = filters;
	folderUpdated = true;
	loadDir(dir);	// simulate a folder update operation

	if (!filters.empty() && !files.contains(file.fileName()))
		loadFileAt(0);

	emit folderFiltersChanged(folderKeywords);
}

QStringList DkImageLoader::getFolderFilters() {
	return folderKeywords;
}

/**
 * Sets the file specified and loads the directory.
 * @param file the file to be set as current file.
 **/ 
void DkImageLoader::setFile(QFileInfo& file) {
	
	this->file = file;
	this->virtualFile = file;
	loadDir(file.absoluteDir());
}

/**
 * Sets the current directory to dir.
 * @param dir the directory to be loaded.
 **/ 
void DkImageLoader::setDir(QDir& dir) {

	//QDir oldDir = file.absoluteDir();
	
	bool valid = loadDir(dir);

	if (valid)
		firstFile();
}

/**
 * Sets a new save directory.
 * @param dir the new save directory.
 **/ 
void DkImageLoader::setSaveDir(QDir& dir) {
	this->saveDir = dir;
}

/**
 * Sets the current image to img.
 * @param img the loader's new image.
 **/ 
void DkImageLoader::setImage(QImage img, QFileInfo editFile) {
	
	if (editFile.exists())
		this->editFile = editFile;

	basicLoader.setImage(img, file);
	sendFileSignal();
}

void DkImageLoader::sendFileSignal() {

	QFileInfo f = (editFile.exists()) ? editFile : file;
	emit updateFileSignal(f, basicLoader.image().size(), editFile.exists(), getTitleAttributeString());
}

/**
 * Returns the current file name.
 * @return QString the file name of the currently loaded file.
 **/ 
QString DkImageLoader::fileName() {
	return file.fileName();
}

QString DkImageLoader::getTitleAttributeString() {

	if (basicLoader.getNumPages() <= 1)
		return QString();

	QString attr = "[" + QString::number(basicLoader.getPageIdx()) + "/" + 
		QString::number(basicLoader.getNumPages()) + "]";

	return attr;
}

// DkCacher --------------------------------------------------------------------

DkCacher::DkCacher(QDir dir, QStringList files) {

	this->dir = dir;
	this->files = files;
	
	isActive = true;
	somethingTodo = false;
	curFileIdx = -1;
	maxFileSize = 50;	// in MB
	curCache = 0;
	maxNumFiles = 100;

	newDir = true;
	updateFiles = false;

	index();
}

/**
 * Creates cache for a new dir.
 * NOTE: the thread needs to be stopped before calling this function!
 * @param dir the new directory
 * @param files	the sorted file list of this directory
 **/ 
void DkCacher::setNewDir(QDir& dir, QStringList& files) {
	
	//QMutexLocker locker(&mutex);
	this->dir = dir;
	this->files = files;
	
	newDir = true;
	//index();
}

void DkCacher::updateDir(QStringList& files) {

	this->files = files;	// this change is done from another thread!
	updateFiles = true;
	qDebug() << "cacher files num: " << files.size();
	// CRITICAL! he crashes here if you turn cacher on -> and open a folder where lot's of files are written automatically
	// we should redesign the cacher anyway


	//index();
}

void DkCacher::index() {

	//QWriteLocker locker(&lock);

	if (newDir) {
		DkTimer dt;

		curCache = 0;	// clear cache size
		cache.clear();

		for (int idx = 0; idx < files.size(); idx++) {
			QFileInfo cFile = QFileInfo(dir, files[idx]);
			cache.append(DkImageCache(cFile));
		}
		newDir = false;
		somethingTodo = true;

		curFileIdx = -1;

		qDebug() << "cache indexed " << files.size() << " files in: " << QString::fromStdString(dt.getTotal());
	}

	if (updateFiles) {

		QVector<DkImageCache> tmpCache;
		curCache = 0;	// clear cache size

		for (int idx = 0; idx < files.size(); idx++) {
			
			DkImageCache tmp(QFileInfo(dir, files[idx]));
			int cacheIdx = cache.indexOf(tmp);
			
			if (cacheIdx != -1) {
				tmpCache.append(cache[cacheIdx]);
				curCache += cache[cacheIdx].getCacheSize();
			}
			else
				tmpCache.append(tmp);
		}

		qDebug() << "cur cache after updating: " << DkUtils::readableByte(curCache);

		cache = tmpCache;

		//curFileIdx = -1;
		somethingTodo = true;
		updateFiles = false;
	}

}

void DkCacher::run() {

	while (true) {

		msleep(100);

		//mutex.lock();
		//lock.lockForRead();

		if (!isActive) {
			qDebug() << "cacher stopped...";
			//lock.unlock();
			break;
		}

		// re-index folder
		if (newDir || updateFiles) {
			//lock.unlock();
			index();
		}

		//lock.lockForRead();

		if (somethingTodo) {
			//lock.unlock();
			load();		// load locks the mutex on it's own
		}
		//else
			//lock.unlock();
	}

}

/**
* Stops the current loading process.
* This method allows for stopping the thread without killing it.
**/ 
void DkCacher::stop() {

	//QMutexLocker locker(&mutex);
	isActive = false;
	qDebug() << "stopping thread: " << this->thread()->currentThreadId();
}

void DkCacher::start() {
	
	isActive = true;
	QThread::start();
}

void DkCacher::pause() {
	
	somethingTodo = false;
	qDebug() << "[cache] pausing cacher...";
}

void DkCacher::play() {
	
	somethingTodo = true;
	qDebug() << "[cache] restarting cacher...";
}

void DkCacher::setCurrentFile(QFileInfo file, QImage img) {

	//QReadLocker locker(&lock);

	QMutableVectorIterator<DkImageCache> cacheIter(cache);

	for (int idx = 0; idx < cache.size(); idx++) {
		
		cacheIter.next();

		if (cache.at(idx).getFile() == file) {
			curFileIdx = idx;
			if (!img.isNull() && cache.at(idx).getCacheState() != DkImageCache::cache_loaded) {
				curCache -= cacheIter.value().getCacheSize();
				
				// 4* since we are dealing with uncompressed images
				if (DkImage::getBufferSizeFloat(img.size(), img.depth()) + curCache < DkSettings::resources.cacheMemory &&
					DkImage::getBufferSizeFloat(img.size(), img.depth()) < 4*maxFileSize) {
					cacheIter.value().setImage(img, true);	// if we get a new cache image here, we can safely assume, that it is already rotated
					curCache += cacheIter.value().getCacheSize();
					qDebug() << "current file set: " << QSize(img.size());
				}
				else
					cacheIter.value().clearImage();
			}
			else if (!img.isNull() && cache.at(idx).getCacheState() == DkImageCache::cache_loaded) {
				cacheIter.value().setImage(img, true);	// don't question the size - it might change due to editing and cause a complete re-caching
			}
			break;
		}

	}

	somethingTodo = true;
}

void DkCacher::load() {

	//QReadLocker locker(&lock);
	somethingTodo = false;

	// invalid file idx
	if (curFileIdx == -1)
		return;

	QMutableVectorIterator<DkImageCache> cacheIter(cache);

	// it's fair enough if we index about +/- 100 images
	for (int idx = 1; idx < maxNumFiles*0.5; idx++) {

		int nIdx = curFileIdx+idx;
		int pIdx = curFileIdx-idx;

		if (nIdx < (int)cache.size() && cache.at(nIdx).getCacheState() == DkImageCache::cache_not_loaded) {
						
			if (!clean(idx))
				break;	// we're done

			// if you know how to directly access the n-th element of a QMutableVectorIterator, please replace the next 3 lines
			cacheIter.toFront();
			for (int cIterIdx = 0; cIterIdx <= nIdx; cIterIdx++)
				cacheIter.next();
				
			if (cacheImage(cacheIter.value())) {	// that might take time
				somethingTodo = true;
				break;	// go to thread to see if some action is waiting
			}
		}
		if (pIdx > 0 && cache.at(pIdx).getCacheState() == DkImageCache::cache_not_loaded) {

			if (!clean(idx))
				break;	// we're done

			cacheIter.toFront();
			for (int cIterIdx = 0; cIterIdx <= pIdx; cIterIdx++)
				cacheIter.next();

			//!! this is important:
			// currently setting a new dir happens in the thread of DkImageLoader (?! - pretty sure)
			// however, this thread is not synced on the vector... so if we change the vector while caching an image
			// bad things happen...
			if (cacheImage(cacheIter.value())) {	// that might take time // TODO: that might crash
				somethingTodo = true;
				break;	// go to thread to see if some action is waiting
			}
		}
	}


}

bool DkCacher::clean(int curCacheIdx) {

	// nothing todo
	if (curCache < DkSettings::resources.cacheMemory)
		return true;
	
	QMutableVectorIterator<DkImageCache> cacheIter(cache);

	for (int idx = 0; idx < (int)cache.size(); idx++) {

		cacheIter.next();

		// skip the current cache region
		if (idx > curFileIdx-curCacheIdx && idx <= curFileIdx+curCacheIdx)
			continue;

		if (cacheIter.value().getCacheState() == DkImageCache::cache_loaded) {
			
			curCache -= cacheIter.value().getCacheSize();
			cacheIter.value().clearImage();	// clear cached image

			qDebug() << "[cache] I cleared: " << cacheIter.value().getFile().fileName() << " cache volume: " << curCache << " MB";
		}
	}

	qDebug() << "[cache] cache volume: " << curCache << " MB";

	// stop caching
	if (curCache >= DkSettings::resources.cacheMemory)
		return false;
	
	return true;
}

bool DkCacher::cacheImage(DkImageCache& cacheImg) {
	
	QFileInfo file = cacheImg.getFile();
	
	// resolve links
	if (file.isSymLink()) file = QFileInfo(file.symLinkTarget());
	QFile f(file.filePath());

	// jpg files must be smaller than 1/4 of the max file size (as they are way larger when loaded
	// ignore files < 100 KB || larger than maxFileSize
	if (f.size() < 100*1024 || f.size() > 1024*1024*maxFileSize || (f.size() > 1024*1024*maxFileSize*0.25f && file.suffix() == "jpg")) {
		qDebug() << "[cache] I ignored: " << cacheImg.getFile().fileName() << " file size: " << f.size()/(1024.0f*1024.0f) << " MB";
		cacheImg.ignore();
		return false;
	}

	QImage img;
	if (loader.loadGeneral(file)) {
		
		cacheImg.setImage(loader.image());
		curCache += cacheImg.getCacheSize();

		qDebug() << "[cache] I cached: " << cacheImg.getFile().fileName() << " cache volume: " << curCache << " MB/ " 
			<< DkSettings::resources.cacheMemory << " MB";
		return true;
	}
	else
		cacheImg.ignore();		// cannot cache image

	return false;
}

}
