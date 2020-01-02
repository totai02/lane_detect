#include "lane_detection.h"

#include "utils.h"
#include "json.hpp"

#define SQUARE(x)(x*x)

float inline euclide_dist(cv::Vec4i l)
{
    return sqrt(SQUARE(l[0] - l[2]) + SQUARE(l[1] - l[3]));
}

float inline angle(cv::Vec4i l) {
    return (atan2(l[2] - l[0], l[3] - l[1]) * 180) / CV_PI;
}

cv::Point inline get_point(cv::Vec4i l, float y)
{
    return cv::Point((y - l[1]) * (l[0] - l[2]) / (l[1] - l[3]) + l[0], y);
}

int int_cmpr(const int a, const int b) 
{
    if (a < b)
        return -1;

    if (a > b)
        return 1;

    return 0;
}

namespace tpv
{
static cv::Vec4i NIL_LANE = cv::Vec4i();

struct DistanceCalc {
    bool operator()(cv::Vec4i a, cv::Vec4i b);
};

bool DistanceCalc::operator()(cv::Vec4i a, cv::Vec4i b)
{
    return abs(angle(a) - angle(b)) < DISTANCE_CALC_BIN_THRESHOLD;
}


LaneDetectorObject::~LaneDetectorObject() { }

LaneDetectorObject::LaneDetectorObject()
{
    int min_thres[3] = MIN_THRESHOLD;
    int max_thres[3] = MAX_THRESHOLD;

    std::memcpy(&_min_threshold[0], &min_thres, sizeof(int) * 3);
    std::memcpy(&_max_threshold[0], &max_thres, sizeof(int) * 3);

    _binary_threshold = BINARY_THRESHOLD;
    _sky_line = VERTICAL_SKY_LINE;
    _lane_width = LANE_WIDTH;

    _width = STATIC_WIDTH;
    _height = STATIC_HEIGHT;
}

LaneDetectorObject::LaneDetectorObject(const int* min_thres,
    const int* max_thres,
    const int bin_thres,
    const int sky_line,
    const int lane_width,
    const int width,
    const int height)
{
    std::memcpy(&_min_threshold[0], min_thres, sizeof(int) * 3);
    std::memcpy(&_max_threshold[0], max_thres, sizeof(int) * 3);

    _binary_threshold = bin_thres;
    _sky_line = sky_line;
    _lane_width = lane_width;

    _width = width;
    _height = height;
}

LaneDetectorObject::LaneDetectorObject(const std::string& json_config_path)
{
    std::ifstream file(json_config_path);
    nlohmann::json json_input;
    
    if (file.is_open())
    {
        file >> json_input;

        std::vector<int> min_thres, max_thres;
        min_thres = json_input.at("min_threshold").get<std::vector<int>>();
        max_thres = json_input.at("max_threshold").get<std::vector<int>>();
        
        std::copy(&min_thres[0], &min_thres[3], _min_threshold);
        std::copy(&max_thres[0], &max_thres[3], _max_threshold);

        _binary_threshold = json_input.at("binary_threshold").get<int>();
        _sky_line = json_input.at("sky_line").get<int>();
        _lane_width = json_input.at("lane_width").get<int>();

        _width = json_input.at("width").get<int>();
        _height = json_input.at("height").get<int>();
    }
}

int LaneDetectorObject::width() {
    return _width;
}

int LaneDetectorObject::height() {
    return _height;
}

void LaneDetectorObject::pre_process(const TPV_CV_MAT& src, TPV_CV_MAT& dst) {
    TPV_CV_MAT im_blurred, im_gray, im_hsv, im_thresholded, im_canny;

    static cv::Scalar lower(_min_threshold[0], _min_threshold[1], _min_threshold[2]);
    static cv::Scalar upper(_max_threshold[0], _max_threshold[1], _max_threshold[2]);

    cv::medianBlur(src, im_blurred, BLUR_KER_SIZE);
    cv::cvtColor(src, im_gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(src, im_hsv, cv::COLOR_BGR2HSV);

    cv::inRange(im_hsv, lower, upper, im_thresholded);

    cv::dilate(im_thresholded, im_thresholded,
                cv::getStructuringElement(cv::MorphShapes::MORPH_ELLIPSE, cv::Size(MORPH_ELLIPSE_KER_SIZE)));
    
    cv::Canny(im_gray, im_canny, CANNY_EDGES, CANNY_THRESHOLD);

    im_canny.copyTo(dst, im_thresholded);
}

void LaneDetectorObject::update(const TPV_CV_MAT& src, TPV_CV_MAT& dst)
{
    TPV_CV_MAT im_binary, im_input;

    cv::resize(src, im_input, cv::Size(_width, _height));    
    pre_process(im_input, im_binary);

    VECTOR<cv::Vec4i> lines;
    fit_lane_2_line(im_binary, lines, 10);

    grp_line(lines);

    if (_left_lane != NIL_LANE)
    {
        cv::Point p1 = get_point(_left_lane, _height);
        cv::Point p2 = get_point(_left_lane, _height / 2);
    
        cv::line(im_input, p1, p2, cv::Scalar(0, 0, 255), 2);
    }

    if (_right_lane != NIL_LANE)
    {
        cv::Point p1 = get_point(_right_lane, _height);
        cv::Point p2 = get_point(_right_lane, _height / 2);
    
        cv::line(im_input, p1, p2, cv::Scalar(0, 255, 0), 2);
    }

    im_input.copyTo(dst);    
}

float LaneDetectorObject::get_err_angle()
{
    cv::Point dst(_width / 2, _height / 2);

    cv::Point p1 = get_point(_left_lane, _height / 2);
    cv::Point p2 = get_point(_right_lane, _width / 2);

    int pr = _pre_lane != NIL_LANE ? get_point(_pre_lane, _height / 2).x : _width / 2;

    if (_left_lane != NIL_LANE && _right_lane != NIL_LANE)
    {
        if (abs((p1.x + p2.x) / 2 - pr) < 30)
        {
            dst.x = (p1.x + p2.x) / 2;
        }
        else
        {
            dst.x = ((p1.x + p2.x) / 2 + pr) / 2;
        }
    }
    else if (_right_lane != NIL_LANE)
    {
        dst.x = p2.x - _lane_width / 2;
    }
    else if (_left_lane != NIL_LANE)
    {
        dst.x = p1.x + _lane_width / 2;
    }

    return _err_angle(dst);
}

float LaneDetectorObject::_err_angle(cv::Point p)
{
    if (p.x == _car_position.x)
    {
        return 0;
    }

    if (p.y == _car_position.y) {
        return (p.x < _car_position.x ? -90 : 90);
    }

    double dx = p.x - _car_position.x;
    double dy = _car_position.y - p.y;

    if (dx < 0) {
        return - atan(-dx / dy) * 180 / CV_PI;
    } else {
        return atan(dx / dy) * 180 / CV_PI;
    }

}

void LaneDetectorObject::grp_line(const VECTOR<cv::Vec4i>& lines)
{
    if (lines.size() == 0)
    {
        return;
    }

    VECTOR<int> labels;

    int count = cv::partition(lines, labels, DistanceCalc());

    int count_line[count];
    int idx[count];
    cv::Vec4i mean[count];

    std::iota(idx, idx + count, 0);

    for (int i = 0; i < labels.size(); i++)
    {
        count_line[labels[i]]++;
        mean[labels[i]] = cv::Vec4i(0, 0, 0, 0);
    }

    // sorting
    // c_bsort<int, int>(count_line, idx, count, int_cmpr);
    c_qsort<int, int>(count_line, idx, count);
    
    for (int i = 0; i < lines.size(); i++)
    {
        mean[idx[labels[i]]] += lines[i];
    }

    for (int i = 0; i < count; i++)
    {
        mean[idx[i]] /= count_line[idx[i]];
    }

    _left_lane = NIL_LANE;
    _right_lane = NIL_LANE;

    if (count >= 2)
    {
        if (abs(angle(mean[0])) > 15 && abs(angle(mean[1])) > 15)
        {
            if (get_point(mean[0], _height).x < get_point(mean[1], _height).x)
            {
                _left_lane = mean[0];
                _right_lane = mean[1];
            }
            else
            {
                _left_lane = mean[1];
                _right_lane = mean[0];
            }
        }
    }
    else
    {
        if (get_point(mean[0], _height).x < _width / 2)
            _left_lane = mean[0];
        else
            _right_lane = mean[0];
    }        
}

void LaneDetectorObject::fit_lane_2_line(const TPV_CV_MAT& src, VECTOR<cv::Vec4i>& vec, float weight)
{
    VECTOR<cv::Vec4i> result;
    TPV_CV_MAT debug = TPV_CV_MAT::zeros(src.size(), CV_8UC1);

    VECTOR<cv::Vec4i> lines;
    cv::HoughLinesP(src, lines, 1, CV_PI / 180, 35, 10, 3);

    for (int i = 0; i < lines.size(); i++)
    {
        float length = euclide_dist(lines[i]);
        float line_angle = angle(lines[i]);

        if (abs(line_angle) < 15)
            continue;

        bool check = true;
        for (int j = 0; j < 2; j++)
        {
            if (lines[i][j * 2 + 1] < _sky_line)
            {
                check = false;
                break;
            }
        }
        if (!check) continue;

        if (weight)
        {
            float p = 0;
            if (lines[i][1] > _height / 3 * 2 ||
                lines[i][3] > _height / 3 * 2)
            {
                p = 10;
            }
            for (int w = 0; w < ceil(length / weight) + p; w++)
            {
                result.push_back(lines[i]);
            }
        }
        else 
        {
            result.push_back(lines[i]);
        }
        
    }

    vec = result;
}

void LaneDetectorObject::create_track_bars() {
   cv::createTrackbar("LowH", "DEV_THRESHOLDERS", &_min_threshold[0], 255);
   cv::createTrackbar("HighH", "DEV_THRESHOLDERS", &_max_threshold[0], 255);

   cv::createTrackbar("LowS", "DEV_THRESHOLDERS", &_min_threshold[1], 255);
   cv::createTrackbar("HighS", "DEV_THRESHOLDERS", &_max_threshold[1], 255);

   cv::createTrackbar("LowV", "DEV_THRESHOLDERS", &_min_threshold[2], 255);
   cv::createTrackbar("HighV", "DEV_THRESHOLDERS", &_max_threshold[2], 255);

   cv::createTrackbar("BinaryGray", "DEV_THRESHOLDERS", &_binary_threshold, 255);
}

std::vector<int> LaneDetectorObject::get_configurations()
{
    std::vector<int> d;
    return d;
}

}