#include "mediapipe/framework/calculator_framework.h"
#include "mediapipe/framework/formats/image_frame.h"
#include "mediapipe/framework/formats/image_frame_opencv.h"
#include "mediapipe/framework/port/integral_types.h"
#include "mediapipe/framework/port/logging.h"
#include "mediapipe/framework/port/opencv_imgproc_inc.h"
#include "mediapipe/framework/port/ret_check.h"
#include "mediapipe/framework/port/source_location.h"
#include "mediapipe/framework/port/status_builder.h"

namespace mediapipe {
namespace {
void SetColorChannel(int channel, uint8 value, cv::Mat* mat) {
  CHECK(mat->depth() == CV_8U);
  CHECK(channel < mat->channels());
  const int step = mat->channels();
  for (int r = 0; r < mat->rows; ++r) {
    uint8* row_ptr = mat->ptr<uint8>(r);
    for (int offset = channel; offset < mat->cols * step; offset += step) {
      row_ptr[offset] = value;
    }
  }
}

constexpr char kRgbaInTag[] = "RGBA_IN";
constexpr char kRgbInTag[] = "RGB_IN";
constexpr char kGrayInTag[] = "GRAY_IN";
constexpr char kRgbaOutTag[] = "RGBA_OUT";
constexpr char kRgbOutTag[] = "RGB_OUT";
constexpr char kGrayOutTag[] = "GRAY_OUT";
}  // namespace

// A portable color conversion calculator calculator.
//
// The following conversions are currently supported, but it's fairly easy to
// add new ones if this doesn't meet your needs--Don't forget to add a test to
// color_convert_calculator_test.cc if you do!
//   RGBA -> RGB
//   GRAY -> RGB
//   RGB  -> GRAY
//   RGB  -> RGBA
//
// This calculator only supports a single input stream and output stream at a
// time. If more than one input stream or output stream is present, the
// calculator will fail at FillExpectations.
// TODO: Remove this requirement by replacing the typed input streams
// with a single generic input and allow multiple simultaneous outputs.
//
// Input streams:
//   RGBA_IN:       The input video stream (ImageFrame, SRGBA).
//   RGB_IN:        The input video stream (ImageFrame, SRGB).
//   GRAY_IN:       The input video stream (ImageFrame, GRAY8).
//
// Output streams:
//   RGBA_OUT:      The output video stream (ImageFrame, SRGBA).
//   RGB_OUT:       The output video stream (ImageFrame, SRGB).
//   GRAY_OUT:      The output video stream (ImageFrame, GRAY8).
class ColorConvertCalculator : public CalculatorBase {
 public:
  ~ColorConvertCalculator() override = default;
  static ::mediapipe::Status GetContract(CalculatorContract* cc);
  ::mediapipe::Status Process(CalculatorContext* cc) override;

 private:
  // Wrangles the appropriate inputs and outputs to perform the color
  // conversion. The ImageFrame on input_tag is converted using the
  // open_cv_convert_code provided and then output on the output_tag stream.
  // Note that the output_format must match the destination conversion code.
  ::mediapipe::Status ConvertAndOutput(const std::string& input_tag,
                                       const std::string& output_tag,
                                       ImageFormat::Format output_format,
                                       int open_cv_convert_code,
                                       CalculatorContext* cc);
};

REGISTER_CALCULATOR(ColorConvertCalculator);

::mediapipe::Status ColorConvertCalculator::GetContract(
    CalculatorContract* cc) {
  RET_CHECK_EQ(cc->Inputs().NumEntries(), 1)
      << "Only one input stream is allowed.";
  RET_CHECK_EQ(cc->Outputs().NumEntries(), 1)
      << "Only one output stream is allowed.";

  if (cc->Inputs().HasTag(kRgbaInTag)) {
    cc->Inputs().Tag(kRgbaInTag).Set<ImageFrame>();
  }

  if (cc->Inputs().HasTag(kGrayInTag)) {
    cc->Inputs().Tag(kGrayInTag).Set<ImageFrame>();
  }

  if (cc->Inputs().HasTag(kRgbInTag)) {
    cc->Inputs().Tag(kRgbInTag).Set<ImageFrame>();
  }

  if (cc->Outputs().HasTag(kRgbOutTag)) {
    cc->Outputs().Tag(kRgbOutTag).Set<ImageFrame>();
  }

  if (cc->Outputs().HasTag(kGrayOutTag)) {
    cc->Outputs().Tag(kGrayOutTag).Set<ImageFrame>();
  }

  if (cc->Outputs().HasTag(kRgbaOutTag)) {
    cc->Outputs().Tag(kRgbaOutTag).Set<ImageFrame>();
  }

  return ::mediapipe::OkStatus();
}

::mediapipe::Status ColorConvertCalculator::ConvertAndOutput(
    const std::string& input_tag, const std::string& output_tag,
    ImageFormat::Format output_format, int open_cv_convert_code,
    CalculatorContext* cc) {
  const cv::Mat& input_mat =
      formats::MatView(&cc->Inputs().Tag(input_tag).Get<ImageFrame>());
  std::unique_ptr<ImageFrame> output_frame(
      new ImageFrame(output_format, input_mat.cols, input_mat.rows));
  cv::Mat output_mat = formats::MatView(output_frame.get());
  cv::cvtColor(input_mat, output_mat, open_cv_convert_code);

  // cv::cvtColor will leave the alpha channel set to 0, which is a bizarre
  // design choice. Instead, let's set alpha to 255.
  if (open_cv_convert_code == cv::COLOR_RGB2RGBA) {
    SetColorChannel(3, 255, &output_mat);
  }
  cc->Outputs()
      .Tag(output_tag)
      .Add(output_frame.release(), cc->InputTimestamp());
  return ::mediapipe::OkStatus();
}

::mediapipe::Status ColorConvertCalculator::Process(CalculatorContext* cc) {
  // RGBA -> RGB
  if (cc->Inputs().HasTag(kRgbaInTag) && cc->Outputs().HasTag(kRgbOutTag)) {
    return ConvertAndOutput(kRgbaInTag, kRgbOutTag, ImageFormat::SRGB,
                            cv::COLOR_RGBA2RGB, cc);
  }
  // GRAY -> RGB
  if (cc->Inputs().HasTag(kGrayInTag) && cc->Outputs().HasTag(kRgbOutTag)) {
    return ConvertAndOutput(kGrayInTag, kRgbOutTag, ImageFormat::SRGB,
                            cv::COLOR_GRAY2RGB, cc);
  }
  // RGB -> GRAY
  if (cc->Inputs().HasTag(kRgbInTag) && cc->Outputs().HasTag(kGrayOutTag)) {
    return ConvertAndOutput(kRgbInTag, kGrayOutTag, ImageFormat::GRAY8,
                            cv::COLOR_RGB2GRAY, cc);
  }
  // RGB -> RGBA
  if (cc->Inputs().HasTag(kRgbInTag) && cc->Outputs().HasTag(kRgbaOutTag)) {
    return ConvertAndOutput(kRgbInTag, kRgbaOutTag, ImageFormat::SRGBA,
                            cv::COLOR_RGB2RGBA, cc);
  }

  return ::mediapipe::InvalidArgumentErrorBuilder(MEDIAPIPE_LOC)
         << "Unsupported image format conversion.";
}

}  // namespace mediapipe
