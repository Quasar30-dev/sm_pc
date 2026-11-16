#pragma once
#include "hig/higaudio.h"
#include "hig/higlevel.h"
#include "hig/higmovie.h"
#include <memory>
namespace sm {
// Original Bink media decoded by an isolated, hidden FFmpeg child. Gameplay
// never runs in that process. A four-frame queue bounds video memory.
class Movie {
  public:
    Movie();
    ~Movie();
    bool open(const hig::IsoFs &, const std::string &, std::string &error);
    void stop();
    bool frame(double seconds, hig::GimImage &out);
    std::shared_ptr<hig::AudioSample> soundtrack() const;
    std::string error() const;
    const hig::MovieInfo &info() const;
    bool decoding_finished() const;
    int displayed_frame() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace sm
