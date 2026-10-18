// story_notebook_test: the notebook without its page layout (story_notebook_layout.h): the page is drawn
// on the HUD, as in a session without a layout. The layout itself is tested in game runs.
#include "mods/story_notebook_layout.h"

namespace story_notebook {
void set_layout_description(const std::string&, const std::string&) {}
void set_icon_open(const std::string&) {}
const std::vector<uint8_t>& icon_open_png() {
    static const std::vector<uint8_t> none;
    return none;
}
}  // namespace story_notebook
namespace story_notebook_layout {
bool configure() { return false; }
bool update(const story_notebook::Page&, const std::vector<story_notebook::Row>&) { return false; }
bool update_closed() { return false; }
bool drawing() { return false; }
bool animating() { return false; }
}  // namespace story_notebook_layout
