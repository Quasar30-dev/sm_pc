#pragma once
#include "scene.h"
#include "render_options.h"
#include <filesystem>
#include <memory>
namespace sm {
struct SceneView {
    std::array<float, 3> eye{}, anchor{};
    float seconds = 0;
    bool sky = true, ocean = true, underwater = false;
    RenderOptions render_options;
};
class RenderDx11 {
  public:
    RenderDx11();
    ~RenderDx11();
    bool init(void *window, unsigned width, unsigned height, bool debug, std::string &error);
    bool upload(const Scene &, const hig::LevelData &, std::string &error);
    bool update_actors(const Scene &, std::string &error);
    bool resize(unsigned width, unsigned height, std::string &error);
    bool draw(const std::array<float, 16> &view_projection, const SceneView &, std::string &error);
    bool present(bool vsync, std::string &error);
    bool capture(const std::filesystem::path &, std::string &error);
    void *device() const;
    void *context() const;
    void *back_buffer() const; // ID3D11Texture2D, non-owning, valid until resize
    unsigned width() const;
    unsigned height() const;
    size_t validation_errors() const;
    void *upload_picture(const hig::GimImage &, std::string &error);
    void *update_movie(const hig::GimImage &, std::string &error);

  private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace sm
