#pragma once
#include <windows.h>
#include <atomic>
#include <future>
#include "flcat/model.hpp"
#include "image.hpp"
#include "studio_surface.hpp"
#include "list_scroll.hpp"
#include "online_images.hpp"
#include <optional>
#include <array>
#include <map>
#include "motion.hpp"

class Panel {
public:
    HWND window = nullptr;
    bool create();
    void close();
    ~Panel() { close(); }
private:
    std::vector<flcat::Entry> entries;
    std::vector<size_t> visible;
    std::optional<size_t> preview_index;
    PreviewImage image,previous_image;
    motion::Tween preview_blend{1};
    std::array<motion::Button,10> buttons;
    bool buffered_paint=false;
    StudioSurface surface;
    HFONT heading=nullptr, bold=nullptr, display=nullptr, mono=nullptr;
    ListScroll purposes_scroll, results_scroll;
    std::map<std::string,size_t> purpose_counts;
    int type_filter=0;
    std::string library_id;
    std::atomic<bool> cancel{false};
    std::future<std::vector<flcat::Entry>> job;
    std::future<artwork::Report> image_job;
    std::atomic<bool> stop_images{false};
    void fill_images();
    void finish_images();
    static INT_PTR CALLBACK dialog(HWND, UINT, WPARAM, LPARAM);
    void init();
    void scan();
    void complete();
    void load_filters();
    void filter();
    void selection_changed();
    void edit(int command);
    void browse();
    void preview(std::optional<size_t> index);
    void animate_preview();
    void choose_image(bool reset);
    void draw(const DRAWITEMSTRUCT& item);
};
