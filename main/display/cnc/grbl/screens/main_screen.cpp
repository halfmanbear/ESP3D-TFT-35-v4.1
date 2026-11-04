/*
  esp3d_tft

  Copyright (c) 2022 Luc Lebosse. All rights reserved.

  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "screens/main_screen.h"
#include "esp3d_tft_ui.h"
#include "pendant/pendant_ui.h"

namespace mainScreen {
// The pendant UI reads one mutex-protected controller snapshot on the LVGL task,
// so the legacy per-value callbacks have nothing left to do.
bool position_values(ESP3DValuesIndex, const char*, ESP3DValuesCbAction) { return true; }
bool state_value_cb(ESP3DValuesIndex, const char*, ESP3DValuesCbAction) { return true; }
bool state_comment_value_cb(ESP3DValuesIndex, const char*, ESP3DValuesCbAction) { return true; }
bool job_status_value_cb(ESP3DValuesIndex, const char*, ESP3DValuesCbAction) { return true; }

void create() {
  esp3dTftui.set_current_screen(ESP3DScreenType::none);
  pendant::ui::show();
  esp3dTftui.set_current_screen(ESP3DScreenType::main);
}
}  // namespace mainScreen
