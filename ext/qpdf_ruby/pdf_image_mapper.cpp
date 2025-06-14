#include "pdf_image_mapper.hpp"
#include <qpdf/BufferInputSource.hh>
#include <qpdf/QPDFTokenizer.hh>

#include <algorithm>
#include <optional>

using Matrix = std::array<double, 6>;

static std::pair<double, double> apply_matrix(const Matrix& m, double x, double y) {
  double x_new = m[0] * x + m[2] * y + m[4];
  double y_new = m[1] * x + m[3] * y + m[5];
  return {x_new, y_new};
}

static std::array<double, 4> compute_bbox(double width, double height, const Matrix& matrix) {
  std::array<std::pair<double, double>, 4> corners = {apply_matrix(matrix, 0, 0), apply_matrix(matrix, width, 0),
                                                      apply_matrix(matrix, width, height),
                                                      apply_matrix(matrix, 0, height)};

  double min_x = corners[0].first;
  double max_x = corners[0].first;
  double min_y = corners[0].second;
  double max_y = corners[0].second;

  for (int i = 1; i < 4; ++i) {
    min_x = std::min(min_x, corners[i].first);
    max_x = std::max(max_x, corners[i].first);
    min_y = std::min(min_y, corners[i].second);
    max_y = std::max(max_y, corners[i].second);
  }

  return {min_x, min_y, max_x, max_y};
}

static char const* tokenTypeName(QPDFTokenizer::token_type_e ttype) {
  // Do this is a case statement instead of a lookup so the compiler
  // will warn if we miss any.
  switch (ttype) {
    case QPDFTokenizer::tt_bad:
      return "bad";
    case QPDFTokenizer::tt_array_close:
      return "array_close";
    case QPDFTokenizer::tt_array_open:
      return "array_open";
    case QPDFTokenizer::tt_brace_close:
      return "brace_close";
    case QPDFTokenizer::tt_brace_open:
      return "brace_open";
    case QPDFTokenizer::tt_dict_close:
      return "dict_close";
    case QPDFTokenizer::tt_dict_open:
      return "dict_open";
    case QPDFTokenizer::tt_integer:
      return "integer";
    case QPDFTokenizer::tt_name:
      return "name";
    case QPDFTokenizer::tt_real:
      return "real";
    case QPDFTokenizer::tt_string:
      return "string";
    case QPDFTokenizer::tt_null:
      return "null";
    case QPDFTokenizer::tt_bool:
      return "bool";
    case QPDFTokenizer::tt_word:
      return "word";
    case QPDFTokenizer::tt_eof:
      return "eof";
    case QPDFTokenizer::tt_space:
      return "space";
    case QPDFTokenizer::tt_comment:
      return "comment";
    case QPDFTokenizer::tt_inline_image:
      return "inline-image";
  }
  return nullptr;
}

static std::optional<ImageInfo> get_image_info(QPDFObjectHandle resources, const std::string& name) {
  if (resources.isNull() || !resources.isDictionary()) {
    return std::nullopt;
  }

  QPDFObjectHandle xobjects = resources.getKey("/XObject");
  if (xobjects.isNull() || !xobjects.isDictionary()) {
    return std::nullopt;
  }

  if (!xobjects.hasKey(name)) {
    return std::nullopt;
  }

  QPDFObjectHandle xobject = xobjects.getKey(name);
  if (!xobject.isStream()) {
    return std::nullopt;
  }

  QPDFObjectHandle dict = xobject.getDict();
  bool is_image =
      (dict.hasKey("/Subtype") && dict.getKey("/Subtype").isName() && dict.getKey("/Subtype").getName() == "/Image");

  if (is_image) {
    ImageInfo image_info;

    if (dict.hasKey("/Height") && dict.getKey("/Height").isInteger()) {
      image_info.height = dict.getKey("/Height").getIntValue();
    }

    if (dict.hasKey("/Width") && dict.getKey("/Width").isInteger()) {
      image_info.width = dict.getKey("/Width").getIntValue();
    }

    return image_info;
  }

  return std::nullopt;
  ;
}

PDFImageMapper::PDFImageMapper(int target_mcid) : target_mcid(target_mcid), in_target_mcid(false) {}

void PDFImageMapper::push_cm(double value) {
  if (cm_fixed_size_queue.size() == 6) {
    cm_fixed_size_queue.pop_front();  // Remove oldest element
  }
  cm_fixed_size_queue.push_back(value);
}

void PDFImageMapper::find(QPDF& pdf) {
  QPDFPageDocumentHelper doc_helper(pdf);
  std::vector<QPDFPageObjectHelper> pages = doc_helper.getAllPages();
  for (auto& page : pages) {
    QPDFPageObjectHelper poh(page);

    auto crop_box = poh.getCropBox();

    this->find(poh);
  }
}

struct OperatorInfo {
  const char* name;
  const char* description;
  int operand_count;
};

static constexpr OperatorInfo OPERATOR_CM = {"cm", "Concatenate matrix (set CTM)", 6};
static constexpr OperatorInfo OPERATOR_Q = {"q", "Save graphics state", 0};
static constexpr OperatorInfo OPERATOR_Q_UPPER = {"Q", "Restore graphics state", 0};
static constexpr OperatorInfo OPERATOR_DO = {"Do", "Invoke named XObject (draw image/form)", 1};
static constexpr OperatorInfo OPERATOR_BDC = {"BDC", "Begin Marked Content sequence with property list", 2};
static constexpr OperatorInfo OPERATOR_EMC = {"EMC", "End Marked Content sequence", 0};

class CMDoExtractor : public QPDFObjectHandle::ParserCallbacks {
 public:
  explicit CMDoExtractor(QPDFPageObjectHelper& page_ref) : page(page_ref) {
    // You can add any other initialization logic here if needed
  }

  std::vector<double> current_matrix = {1, 0, 0, 1, 0, 0};
  std::stack<std::vector<double>> matrix_stack;
  std::vector<QPDFObjectHandle> operand_stack;

  std::stack<int> mcid_stack;
  int current_mcid;

  void handleEOF() override {
    // No action needed for this example
  }

  std::optional<ImageInfo> image_info(std::string imgName) {
    QPDFObjectHandle resources = page.getObjectHandle().getKey("/Resources");

    return get_image_info(resources, imgName);
  }

  void handleObject(QPDFObjectHandle obj, size_t offset, size_t length) override {
    if (obj.isOperator()) {
      std::string op = obj.getOperatorValue();

      if (op == OPERATOR_CM.name && operand_stack.size() >= OPERATOR_CM.operand_count) {
        auto numeric_at = [&](std::size_t distance_from_top) -> double {
          auto& obj = operand_stack[operand_stack.size() - distance_from_top];
          auto t = obj.getTypeName();  // "integer", "real", …
          if (std::strcmp(t, "integer") != 0 && std::strcmp(t, "real") != 0) {
            std::stringstream ss;
            ss << "numeric operand expected (got " << t << ")";
            throw std::runtime_error(ss.str());
          }
          return obj.getNumericValue();
        };

        // pull operands (top of stack is distance 1)
        double f = numeric_at(1);
        double e = numeric_at(2);
        double d = numeric_at(3);
        double c = numeric_at(4);
        double b = numeric_at(5);
        double a = numeric_at(6);
        current_matrix = {a, b, c, d, e, f};

        operand_stack.resize(operand_stack.size() - 6);
      } else if (op == OPERATOR_Q.name) {
        matrix_stack.push(current_matrix);
      } else if (op == OPERATOR_Q_UPPER.name) {
        if (!matrix_stack.empty()) {
          current_matrix = matrix_stack.top();
          matrix_stack.pop();
        }
      } else if (op == OPERATOR_BDC.name && operand_stack.size() >= OPERATOR_BDC.operand_count) {
        mcid_stack.push(current_mcid);  // Save current MCID

        QPDFObjectHandle properties_obj = operand_stack.back();  // Properties operand
        // QPDFObjectHandle tag_obj = operand_stack[operand_stack.size() - 2]; // Tag operand

        int mcid_val = -1;  // Default if MCID not found or not an integer
        if (properties_obj.isDictionary()) {
          if (properties_obj.hasKey("/MCID") && properties_obj.getKey("/MCID").isInteger()) {
            mcid_val = properties_obj.getKey("/MCID").getIntValue();
          }
        } else if (properties_obj.isName()) {
          // Properties is a name, look it up in Resources /Properties dictionary
          QPDFObjectHandle resources = page.getObjectHandle().getKey("/Resources");
          if (resources.isDictionary() && resources.hasKey("/Properties")) {
            QPDFObjectHandle properties_dict = resources.getKey("/Properties");
            if (properties_dict.isDictionary() && properties_dict.hasKey(properties_obj.getName())) {
              QPDFObjectHandle actual_props = properties_dict.getKey(properties_obj.getName());
              if (actual_props.isDictionary() && actual_props.hasKey("/MCID") &&
                  actual_props.getKey("/MCID").isInteger()) {
                mcid_val = actual_props.getKey("/MCID").getIntValue();
              }
            }
          }
        }
        current_mcid = mcid_val;
        operand_stack.resize(operand_stack.size() - OPERATOR_BDC.operand_count);
      } else if (op == OPERATOR_EMC.name) {  // EMC takes 0 operands
        if (!mcid_stack.empty()) {
          current_mcid = mcid_stack.top();
          mcid_stack.pop();
        } else {
          current_mcid = -1;  // Reset to default if stack underflow (should be balanced)
        }
        // EMC takes 0 operands, so no change to operand_stack based on operand_count
      } else if (op == OPERATOR_DO.name && !operand_stack.empty()) {
        std::string imgName = operand_stack.back().getName();
        auto image_info = this->image_info(imgName);

        std::copy(current_matrix.begin(), current_matrix.end(), image_info->cm_matrix.begin());

        image_info->mcid = current_mcid;

        image_info->bbox = compute_bbox(image_info->width, image_info->height, image_info->cm_matrix);

        QPDFObjectHandle mb = page.getObjectHandle().getKey("/MediaBox");

        double px0 = mb.getArrayItem(0).getNumericValue();
        double py0 = mb.getArrayItem(1).getNumericValue();
        double px1 = mb.getArrayItem(2).getNumericValue();
        double py1 = mb.getArrayItem(3).getNumericValue();

        auto& bb = image_info->bbox;   // shorthand reference
        bb[0] = std::max(bb[0], px0);  // left  edge
        bb[1] = std::max(bb[1], py0);  // bottom edge
        bb[2] = std::min(bb[2], px1);  // right edge
        bb[3] = std::min(bb[3], py1);  // top   edge

        image_to_mcid[imgName] = *image_info;

        operand_stack.pop_back();
      } else {
        // For other operators, just clear the operand stack
        operand_stack.clear();
      }
    } else {
      operand_stack.push_back(obj);
    }
  }

  const std::map<std::string, ImageInfo>& getImageMap() const { return image_to_mcid; }

 private:
  QPDFPageObjectHelper& page;
  std::map<std::string, ImageInfo> image_to_mcid;
};

void PDFImageMapper::find(QPDFPageObjectHelper& page) {
  CMDoExtractor cb(page);
  page.parseContents(&cb);

  const auto& extracted_map = cb.getImageMap();
  image_to_mcid.insert(extracted_map.begin(), extracted_map.end());
}
