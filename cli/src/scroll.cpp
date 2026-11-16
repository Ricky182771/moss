#include "scroll.h"

#include <ftxui/dom/node.hpp>

#include <memory>
#include <utility>

namespace chatbot::cli {

namespace {

/// Igual que ftxui::reflect, pero guarda la caja completa que recibe el
/// elemento sin recortarla a la zona visible. Dentro de un frame, eso es el
/// alto total del contenido, que hace falta para limitar el scroll.
class MeasureBox : public ftxui::Node {
public:
    MeasureBox(ftxui::Element child, ftxui::Box& box)
        : ftxui::Node(ftxui::Elements{std::move(child)}), box_(box) {}

    void ComputeRequirement() override {
        ftxui::Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }

    void SetBox(ftxui::Box box) override {
        box_ = box;
        ftxui::Node::SetBox(box);
        children_[0]->SetBox(box);
    }

private:
    ftxui::Box& box_;
};

} // namespace

ftxui::Element Scroll::apply(ftxui::Element content) {
    ftxui::Element measured = std::make_shared<MeasureBox>(std::move(content), content_box_);
    // Frame::SetBox centra el foco: con el foco en top + (alto - 1) / 2 la
    // primera línea visible es exactamente top (FTXUI v7.0.3, frame.cpp).
    ftxui::Element focused =
        follow_ ? measured | ftxui::focusPositionRelative(0.f, 1.f)
                : measured | ftxui::focusPosition(0, top_ + (view_height() - 1) / 2);
    return focused | ftxui::yframe | ftxui::reflect(view_box_) | ftxui::flex;
}

} // namespace chatbot::cli
