/*****************************************************************************
*   ExploringSfMWithOpenCV
******************************************************************************
*   by Roy Shilkrot, 5th Dec 2012
*   http://www.morethantechnical.com/
******************************************************************************
*   Ch4 of the book "Mastering OpenCV with Practical Computer Vision Projects"
*   Copyright Packt Publishing 2012.
*   http://www.packtpub.com/cool-projects-with-opencv/book
*****************************************************************************/

#include "Common.h"

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <iostream>

#ifndef WIN32
#include <dirent.h>
#endif

using namespace std;
using namespace cv;

std::vector<cv::DMatch> FlipMatches(const std::vector<cv::DMatch>& matches) {
	std::vector<cv::DMatch> flip;
	for(int i=0;i<matches.size();i++) {
		flip.push_back(matches[i]);
		swap(flip.back().queryIdx,flip.back().trainIdx);
	}
	return flip;
}

std::vector<cv::Point3d> CloudPointsToPoints(const std::vector<CloudPoint> cpts) {
	std::vector<cv::Point3d> out;
	for (unsigned int i=0; i<cpts.size(); i++) out.push_back(cpts[i].pt);
	return out;
}

void GetAlignedPointsFromMatch(const std::vector<cv::KeyPoint>& imgpts1, const std::vector<cv::KeyPoint>& imgpts2, 
							   const std::vector<cv::DMatch>& matches, std::vector<cv::KeyPoint>& pt_set1, std::vector<cv::KeyPoint>& pt_set2) 
{
	for (unsigned int i=0; i<matches.size(); i++) {
//		cout << "matches[i].queryIdx " << matches[i].queryIdx << " matches[i].trainIdx " << matches[i].trainIdx << endl;
		assert(matches[i].queryIdx < imgpts1.size());
		pt_set1.push_back(imgpts1[matches[i].queryIdx]);
		assert(matches[i].trainIdx < imgpts2.size());
		pt_set2.push_back(imgpts2[matches[i].trainIdx]);
	}	
}

#define intrpmnmx(val,min,max) (max==min ? 0.0 : ((val)-min)/(max-min))
void drawArrows(Mat& frame, const vector<Point2f>& prevPts, const vector<Point2f>& nextPts, 
				const vector<uchar>& status, const vector<float>& verror, const Scalar& _line_color)
{
	double minVal,maxVal; minMaxIdx(verror,&minVal,&maxVal,0,0,status);
	int line_thickness = 1;
	
    for (size_t i = 0; i < prevPts.size(); ++i)
    {
        if (status[i])
        {			
			double alpha = intrpmnmx(verror[i],minVal,maxVal); alpha = 1.0 - alpha;
			Scalar line_color(alpha*_line_color[0],
							  alpha*_line_color[1],
							  alpha*_line_color[2]);

            Point p = prevPts[i];
            Point q = nextPts[i];
            
            double angle = atan2((double) p.y - q.y, (double) p.x - q.x);
            
            double hypotenuse = sqrt( (double)(p.y - q.y)*(p.y - q.y) + (double)(p.x - q.x)*(p.x - q.x) );
            
            if (hypotenuse < 1.0)
                continue;
            
            // Here we lengthen the arrow by a factor of three.
            q.x = (int) (p.x - 3 * hypotenuse * cos(angle));
            q.y = (int) (p.y - 3 * hypotenuse * sin(angle));
            
            // Now we draw the main line of the arrow.
            line(frame, p, q, line_color, line_thickness);
            
            // Now draw the tips of the arrow. I do some scaling so that the
            // tips look proportional to the main line of the arrow.
            
            p.x = (int) (q.x + 9 * cos(angle + CV_PI / 4));
            p.y = (int) (q.y + 9 * sin(angle + CV_PI / 4));
            line(frame, p, q, line_color, line_thickness);
            
            p.x = (int) (q.x + 9 * cos(angle - CV_PI / 4));
            p.y = (int) (q.y + 9 * sin(angle - CV_PI / 4));
            line(frame, p, q, line_color, line_thickness);
        }
    }
}

bool hasEnding (std::string const &fullString, std::string const &ending)
{
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

bool hasEndingLower (string const &fullString_, string const &_ending)
{
	string fullstring = fullString_, ending = _ending;
	transform(fullString_.begin(),fullString_.end(),fullstring.begin(),::tolower); // to lower
	return hasEnding(fullstring,ending);
}

inline std::string changeFileExtension(const std::string &file_path, const std::string &new_ext)
{
	assert(!(new_ext.empty() || new_ext.front() == '.'));
	return file_path + '.' + new_ext;
	//size_t slash_pos = file_path.find_last_of("/\\");
	//return file_path.substr(0, std::min(
	//		file_path.length(),	file_path.find_last_of('.', slash_pos == std::string::npos ? 0 : slash_pos)
	//	)) + '.' + new_ext;
}

//void imshow_250x250(const string& name_, const Mat& patch) {
//	Mat bigpatch; cv::resize(patch,bigpatch,Size(250,250));
//	imshow(name_,bigpatch);
//}

void load_calibration_data(const std::string &file_name, cv::Mat &intrinsics_common, cv::Mat &distortion_vector)
{
	if (file_name.empty()) return;

	intrinsics_common.release();
	distortion_vector.release();

	cv::FileStorage fs;
	if(fs.open(file_name, cv::FileStorage::READ | cv::FileStorage::FORMAT_XML, "utf8")) {
		fs["camera_distortion"] >> distortion_vector;
		fs["camera_intrinsics"] >> intrinsics_common;
		fs.release();
	} else {
		//no calibration matrix file - mockup calibration	
		distortion_vector	= cv::Mat_<double>::zeros(1,4);
		intrinsics_common	= cv::Mat_<double>(3,3) <<	1.0,	0.0,	0.5, 
														0.0,	1.0,	0.5, 
														0.0,	0.0,	1.0;
	}
}

#include <boost/filesystem.hpp>
#include <boost/crc.hpp>

#define CACHE_FILE_EXT "xml"

bool load_images_data(const string &dir_name, std::vector<cv::Mat>& images, std::vector<std::string>& images_names, std::vector<std::vector<cv::KeyPoint>> *features_cache, double downscale_factor) 
{
	images.clear();
	images_names.clear();

	if (dir_name.empty()) return false;

	string _dir_name = dir_name;
	vector<string> files_;

#ifndef WIN32
//open a directory the POSIX way

	DIR *dp;
	struct dirent *ep;     
	dp = opendir (dir_name);
	
	if (dp != NULL)
	{
		while (ep = readdir (dp)) {
			if (ep->d_name[0] != '.')
				files_.push_back(ep->d_name);
		}
		
		(void) closedir (dp);
	}
	else return false;

#else
//open a directory the WIN32 way
	HANDLE hFind = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATA fdata;

	if(_dir_name.back() == '\\' || _dir_name.back() == '/') _dir_name.pop_back();

	hFind = FindFirstFile(string(_dir_name).append("\\*").c_str(), &fdata);	
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (strcmp(fdata.cFileName, ".") != 0 &&
				strcmp(fdata.cFileName, "..") != 0)
			{
				if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					continue; // a diretory
				}
				else
				{
					files_.push_back(fdata.cFileName);
				}
			}
		}
		while (FindNextFile(hFind, &fdata) != 0);
	} else return false;

	if (GetLastError() != ERROR_NO_MORE_FILES)
	{
		FindClose(hFind);
		return false;
	}

	FindClose(hFind);
	hFind = INVALID_HANDLE_VALUE;
#endif
	
	for (unsigned int i=0; i<files_.size(); i++) {
		if (files_[i][0] == '.' || !(hasEndingLower(files_[i],"jpg")||hasEndingLower(files_[i],"png"))) continue;

		cv::Mat m_ = cv::imread(string(_dir_name).append("/").append(files_[i]));
		if (!m_.empty())
		{
			if(downscale_factor != 1.0)	cv::resize(m_,m_,Size(),downscale_factor,downscale_factor);
			images_names.push_back(files_[i]);
			images.push_back(m_);
		}
	}

	if (features_cache != NULL)
	{
		cv::FileStorage		fs;
		boost::crc_32_type	crc;

		features_cache->clear(); features_cache->resize(images.size());
		for (size_t i = 0, imax = images.size(); i < imax; ++i)
		{
			std::string file_name = _dir_name + '/' + images_names[i];
			fs.open(changeFileExtension(file_name, CACHE_FILE_EXT), cv::FileStorage::READ | cv::FileStorage::FORMAT_XML, "utf8");
			if (!fs.isOpened()) continue;

			int		chk_sum;	fs["image_crc"]		>> chk_sum;
			double	dwn_scale;	fs["image_scale"]	>> dwn_scale;

			cv::Mat m_ = images[i];
			crc.reset(); crc.process_block(m_.datastart, m_.dataend); 

			if (unsigned(chk_sum) == crc.checksum() && dwn_scale == downscale_factor)
				cv::read(fs["features_cache"], (*features_cache)[i]);

			fs.release();
		}
	}
		
	return !images.empty();
}

bool save_features_cache(const std::string &dir_name, const std::vector<std::string>& image_names, const std::vector<cv::Mat> &images, const std::vector<std::vector<cv::KeyPoint>> &features_cache, double downscale_factor)
{
	assert(image_names.size() == images.size() && images.size() == features_cache.size());

	std::string _dir_name = dir_name;
	if(_dir_name.back() == '\\' || _dir_name.back() == '/') _dir_name.pop_back();

	if (!boost::filesystem::exists(_dir_name)) return false;

	cv::FileStorage fs;

	boost::crc_32_type crc;
	for (int i = 0, imax = images.size(); i < imax; ++i)
	{
		std::string file_path = _dir_name + '/' + image_names[i];
		if (!boost::filesystem::exists(file_path)) return false;

		fs.open(changeFileExtension(file_path, CACHE_FILE_EXT), cv::FileStorage::WRITE | cv::FileStorage::FORMAT_XML, "utf8");
		if (!fs.isOpened() || features_cache[i].empty()) continue;

		cv::Mat m_ = images[i];
		crc.reset(); crc.process_block(m_.datastart, m_.dataend); 
		fs << "image_crc"		<< int(crc.checksum());
		fs << "image_scale"		<< downscale_factor;
		cv::write(fs, "features_cache", features_cache[i]);

		fs.release();
	}

	return true;
}


