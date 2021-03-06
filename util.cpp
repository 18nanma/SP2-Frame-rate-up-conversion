/*
****************************************
* This file contains the definitions of
* the utility functions used in the 
* algorithm.
* Author : Shehyaaz Khan Nayazi
****************************************
*/

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <fstream>
#include "constants.hpp"
#include "opencv_methods.hpp"
#include "util.hpp"

using namespace cv;
using namespace std;

vector<Mat> readImg()
{
    /* reads the images from the video folder */
    vector<cv::String> fn;
    vector<Mat> images;
    glob("video/*.jpg", fn, false);

    size_t count = fn.size();         //number of jpg files in video folder
    for (size_t i = 0; i < 3; i += 2) // skipping alternate files, which will be interpolated
    {
        Mat img = imread(fn[i]);
        if (!img.data)
        {
            cout << "Could not open or find the image" << std::endl;
            exit(-1);
        }
        images.push_back(img);
    }
    return images;
}

vector<Point2f> phaseCorr(InputArray _src1, InputArray _src2, InputArray _window, double *response = 0)
{
    /* performs customised phase plane correlation on the input frames */
    Mat src1 = _src1.getMat();
    Mat src2 = _src2.getMat();
    Mat window = _window.getMat();

    CV_Assert(src1.type() == src2.type());
    CV_Assert(src1.type() == CV_32FC1 || src1.type() == CV_64FC1);
    CV_Assert(src1.size == src2.size);

    if (!window.empty())
    {
        CV_Assert(src1.type() == window.type());
        CV_Assert(src1.size == window.size);
    }

    int M = getOptimalDFTSize(src1.rows);
    int N = getOptimalDFTSize(src1.cols);

    Mat padded1, padded2, paddedWin;

    if (M != src1.rows || N != src1.cols)
    {
        copyMakeBorder(src1, padded1, 0, M - src1.rows, 0, N - src1.cols, BORDER_CONSTANT, Scalar::all(0));
        copyMakeBorder(src2, padded2, 0, M - src2.rows, 0, N - src2.cols, BORDER_CONSTANT, Scalar::all(0));

        if (!window.empty())
        {
            copyMakeBorder(window, paddedWin, 0, M - window.rows, 0, N - window.cols, BORDER_CONSTANT, Scalar::all(0));
        }
    }
    else
    {
        padded1 = src1;
        padded2 = src2;
        paddedWin = window;
    }

    Mat FFT1, FFT2, P, Pm, C;

    // perform window multiplication if available
    if (!paddedWin.empty())
    {
        // apply window to both images before proceeding...
        multiply(paddedWin, padded1, padded1);
        multiply(paddedWin, padded2, padded2);
    }

    // execute phase correlation equation
    // Reference: http://en.wikipedia.org/wiki/Phase_correlation
    dft(padded1, FFT1, DFT_REAL_OUTPUT);
    dft(padded2, FFT2, DFT_REAL_OUTPUT);

    mulSpectrums(FFT1, FFT2, P, 0, true);

    magSpectrums(P, Pm);
    divSpectrums(P, Pm, C, 0, false); // FF* / |FF*| (phase correlation equation completed here...)

    idft(C, C); // gives us the nice peak shift location...

    fftShift(C); // shift the energy to the center of the frame.

    // locate the highest peak
    Point peakLoc;
    // return two peak locations
    minMaxLoc(C, NULL, NULL, NULL, &peakLoc);

    // get the phase shift with sub-pixel accuracy, 5x5 window seems about right here...
    Point2f t1, t2;
    t1 = weightedCentroid(C, peakLoc, Size(5, 5), response);
    C.at<float>(peakLoc) = 0;                 // set the value at peakLoc to 0
    minMaxLoc(C, NULL, NULL, NULL, &peakLoc); // find second peakLoc
    t2 = weightedCentroid(C, peakLoc, Size(5, 5), response);

    // max response is M*N (not exactly, might be slightly larger due to rounding errors)
    if (response)
        *response /= M * N;

    // adjust shift relative to image center...
    Point2f center((double)padded1.cols / 2.0, (double)padded1.rows / 2.0);

    return {(center - t1), (center - t2)};
}

float calcSAD(Mat prevBlock, int rowpos, int colpos, Mat curr, float dx, float dy)
{
    CV_Assert(prevBlock.type() == curr.type());
    CV_Assert(prevBlock.type() == CV_32FC1 || prevBlock.type() == CV_64FC1);

    float SAD = 0.0; // to store SAD value
    int y = rowpos * BLOCK_SIZE;
    int x = colpos * BLOCK_SIZE;
    int dx_int = (int)round(dx);
    int dy_int = (int)round(dy);
    for (int i = 0; i < prevBlock.rows; i++) // or i < BLOCK_SIZE
    {
        for (int j = 0; j < prevBlock.cols; j++) // or j < BLOCK_SIZE
        {
            if ((i + y + dy_int) < 0 || (i + y + dy_int) >= curr.rows || (j + x + dx_int) < 0 || (j + x + dx_int) >= curr.cols)
                SAD += prevBlock.at<float>(i, j);
            else
                SAD += abs(prevBlock.at<float>(i, j) - curr.at<float>((i + y + dy_int), (j + x + dx_int)));
        }
    }
    return SAD;
}

Point2f medianNeighbor(int rowpos, int colpos, vector<vector<Point2f>> &prevBlockMV)
{
    // median of Point(x,y) = {median of x-coordinates, median of y-coordinates}
    vector<Point2f> neighborMV(3, Point2f(0, 0));
    vector<float> x_coord(3, 0.0);
    vector<float> y_coord(3, 0.0);

    /* we are considering the three nearest neighbors here */
    if (rowpos - 1 < 0 && colpos - 1 < 0)
    {
        // block is in the top-left corner
        neighborMV[0] = prevBlockMV[rowpos][colpos + 1];
        neighborMV[1] = prevBlockMV[rowpos + 1][colpos];
        neighborMV[2] = prevBlockMV[rowpos + 1][colpos + 1];
    }
    else if (colpos - 1 < 0)
    {
        // block is along the left edge
        neighborMV[0] = prevBlockMV[rowpos - 1][colpos];
        neighborMV[1] = prevBlockMV[rowpos - 1][colpos + 1];
        neighborMV[2] = prevBlockMV[rowpos][colpos + 1];
    }
    else if (rowpos - 1 < 0)
    {
        // block is in the top-right corner
        neighborMV[0] = prevBlockMV[rowpos][colpos - 1];
        neighborMV[1] = prevBlockMV[rowpos + 1][colpos - 1];
        neighborMV[2] = prevBlockMV[rowpos + 1][colpos];
    }
    else
    {
        // block is in the middle region
        neighborMV[0] = prevBlockMV[rowpos - 1][colpos - 1];
        neighborMV[1] = prevBlockMV[rowpos - 1][colpos];
        neighborMV[2] = prevBlockMV[rowpos][colpos - 1];
    }
    // find the median
    for (auto point : neighborMV)
    {
        x_coord.push_back(point.x);
        y_coord.push_back(point.y);
    }
    // median of x-coordinates
    const auto median_it_x = x_coord.begin() + x_coord.size() / 2;
    std::nth_element(x_coord.begin(), median_it_x, x_coord.end());
    // median of y-coordinates
    const auto median_it_y = y_coord.begin() + y_coord.size() / 2;
    std::nth_element(y_coord.begin(), median_it_y, y_coord.end());

    Point2f median = Point2f(*median_it_x, *median_it_y);
    return median;
}

void writeToFile(ofstream &file, chrono::milliseconds duration)
{
    if (!file)
    {
        cout << "Could not open the file\n";
        exit(-1);
    }
    file << "Interpolated frame in :" << duration.count() << " milliseconds \n";
}

/*
cv::Point2d cv::phaseCorrelate(InputArray _src1, InputArray _src2, InputArray _window, double* response)
{
    Mat src1 = _src1.getMat();
    Mat src2 = _src2.getMat();
    Mat window = _window.getMat();

    CV_Assert( src1.type() == src2.type());
    CV_Assert( src1.type() == CV_32FC1 || src1.type() == CV_64FC1 );
    CV_Assert( src1.size == src2.size);

    if(!window.empty())
    {
        CV_Assert( src1.type() == window.type());
        CV_Assert( src1.size == window.size);
    }

    int M = getOptimalDFTSize(src1.rows);
    int N = getOptimalDFTSize(src1.cols);

    Mat padded1, padded2, paddedWin;

    if(M != src1.rows || N != src1.cols)
    {
        copyMakeBorder(src1, padded1, 0, M - src1.rows, 0, N - src1.cols, BORDER_CONSTANT, Scalar::all(0));
        copyMakeBorder(src2, padded2, 0, M - src2.rows, 0, N - src2.cols, BORDER_CONSTANT, Scalar::all(0));

        if(!window.empty())
        {
            copyMakeBorder(window, paddedWin, 0, M - window.rows, 0, N - window.cols, BORDER_CONSTANT, Scalar::all(0));
        }
    }
    else
    {
        padded1 = src1;
        padded2 = src2;
        paddedWin = window;
    }

    Mat FFT1, FFT2, P, Pm, C;

    // perform window multiplication if available
    if(!paddedWin.empty())
    {
        // apply window to both images before proceeding...
        multiply(paddedWin, padded1, padded1);
        multiply(paddedWin, padded2, padded2);
    }

    // execute phase correlation equation
    // Reference: http://en.wikipedia.org/wiki/Phase_correlation
    dft(padded1, FFT1, DFT_REAL_OUTPUT);
    dft(padded2, FFT2, DFT_REAL_OUTPUT);

    mulSpectrums(FFT1, FFT2, P, 0, true);

    magSpectrums(P, Pm);
    divSpectrums(P, Pm, C, 0, false); // FF* / |FF*| (phase correlation equation completed here...)

    idft(C, C); // gives us the nice peak shift location...

    fftShift(C); // shift the energy to the center of the frame.

    // locate the highest peak
    Point peakLoc;
    minMaxLoc(C, NULL, NULL, NULL, &peakLoc);

    // get the phase shift with sub-pixel accuracy, 5x5 window seems about right here...
    Point2d t;
    t = weightedCentroid(C, peakLoc, Size(5, 5), response);

    // max response is M*N (not exactly, might be slightly larger due to rounding errors)
    if(response)
        *response /= M*N;

    // adjust shift relative to image center...
    Point2d center((double)padded1.cols / 2.0, (double)padded1.rows / 2.0);

    return (center - t);
}

*/
