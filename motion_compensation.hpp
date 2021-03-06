/*
****************************************
* This file contains the code to 
* contruct the interpolated frame.
* Author : Shehyaaz Khan Nayazi
****************************************
*/
#ifndef MOTION_HPP
#define MOTION_HPP

#include "opencv2/core.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"

using namespace cv;
using namespace std;

Mat bidirectionalMotionCompensation(vector<vector<Mat>> &prevBlocks, Mat curr, vector<vector<Point2f>> &prevBlocksMV);

#endif