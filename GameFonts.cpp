#include "GameFonts.hpp"

#include "data_path.hpp"

Load< Font > ui_font(LoadTagDefault, []() -> Font const * {
	return new Font(data_path("PatrickHand-Regular.ttf"), 48);
});

Load< Font > title_font(LoadTagDefault, []() -> Font const * {
	return new Font(data_path("PatrickHand-Regular.ttf"), 128);
});
