#pragma once

// 小地图雷达总绘制入口，在 Menu::Draw() 中调用。
// 内部调用 Minimap::ScanEntities() 获取数据，然后绘制雷达。
void DrawMinimap();