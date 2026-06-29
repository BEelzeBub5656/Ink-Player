// Ink-Player 外壳 — 相框式
// 参数化设计，所有尺寸可调

/* [Dimensions] */
// 屏幕
SCREEN_W = 170.2;
SCREEN_H = 111.2;
SCREEN_T = 1.25;

// 喇叭 (3525: 35×25×7mm)
SPK_W = 35;
SPK_H = 25;
SPK_T = 7;

// 电池
BAT_W = 50;
BAT_H = 30;
BAT_T = 6;

// C02 转接板
C02_W = 41;
C02_H = 22;
C02_T = 8;             // 含元件高度，待确认

// 外壳
BEZEL = 5;             // 边框宽度
WALL = 2;              // 壁厚
TOTAL_T = 12;          // 总厚度
FRONT_T = 2;           // 前面板厚度
BACK_T = 1.5;          // 背板厚度

// 计算
FRAME_W = SCREEN_W + BEZEL * 2;
FRAME_H = SCREEN_H + BEZEL * 2;
INNER_T = TOTAL_T - FRONT_T - BACK_T;

/* [Features] */
$fn = 64;

// ========== 前面板 ==========
module front_panel() {
    spk_x = FRAME_W/2 - SPK_W/2;
    spk_y = 10;
    
    difference() {
        // 外框
        rounded_rect(FRAME_W, FRAME_H, FRONT_T, 3);
        
        // 屏幕窗口
        translate([BEZEL, (FRAME_H - SCREEN_H)/2, -0.1])
            cube([SCREEN_W, SCREEN_H, FRONT_T + 0.2]);
        
        // 喇叭孔阵 (35×25mm 矩形区域)
        for (sx = [spk_x+4 : 4 : spk_x+SPK_W-4]) {
            for (sy = [spk_y+4 : 4 : spk_y+SPK_H-4]) {
                translate([sx, sy, -0.1])
                    cylinder(h = FRONT_T + 0.2, d = 1.8);
            }
        }
        
        // 麦克风孔（喇叭左侧）
        translate([spk_x - 8, spk_y + SPK_H/2, -0.1])
            cylinder(h = FRONT_T + 0.2, d = 2);
    }
}

// ========== 中框 ==========
module mid_frame() {
    spk_x = FRAME_W/2 - SPK_W/2;
    spk_y = 10;
    
    difference() {
        // 外框
        rounded_rect(FRAME_W, FRAME_H, INNER_T, 3);
        
        // 挖空内部
        translate([WALL, WALL, -0.1])
            cube([FRAME_W - WALL*2, FRAME_H - WALL*2, INNER_T + 0.2]);
        
        // Type-C 口（底部右侧）
        translate([FRAME_W - BEZEL - 12, -0.1, INNER_T/2 - 2])
            cube([10, WALL + 0.2, 4]);
        
        // 电源开关（底部左侧）
        translate([BEZEL + 5, -0.1, INNER_T/2 - 2])
            cube([6, WALL + 0.2, 4]);
        
        // 按钮 ×2（右侧）
        for (b = [0.35, 0.65]) {
            translate([FRAME_W - 0.1, FRAME_H * b, INNER_T/2])
            rotate([0, 90, 0])
                cylinder(h = WALL + 0.2, d = 3.5);
        }
        
        // 喇叭安装槽（中框底部开矩形，喇叭从内侧放入）
        translate([spk_x, spk_y, INNER_T - SPK_T + 1])
            cube([SPK_W, SPK_H, SPK_T + 0.2]);
    }
    
    // PCB 固定螺柱 (M2, 4个角)
    pcb_x = BEZEL + 10;
    pcb_y = (FRAME_H - SCREEN_H)/2 + 10;
    for (dx = [0, 50], dy = [0, 30]) {
        translate([pcb_x + dx, pcb_y + dy, 0])
        difference() {
            cylinder(h = INNER_T, d = 5);
            cylinder(h = INNER_T + 0.1, d = 2.2);  // M2 螺孔
        }
    }
    
    // 电池仓挡板
    bat_x = BEZEL + 5;
    bat_y = FRAME_H - BEZEL - BAT_H - 5;
    translate([bat_x, bat_y, 0])
        cube([BAT_W + 2, WALL, 8]);
    translate([bat_x, bat_y + BAT_H + 2, 0])
        cube([BAT_W + 2, WALL, 8]);
}

// ========== 背板 ==========
module back_panel() {
    difference() {
        rounded_rect(FRAME_W, FRAME_H, BACK_T, 3);
        
        // 螺丝孔（4 角，M3 沉头）
        for (x = [BEZEL + 5, FRAME_W - BEZEL - 5]) {
            for (y = [BEZEL + 5, FRAME_H - BEZEL - 5]) {
                translate([x, y, -0.1])
                    cylinder(h = BACK_T + 0.2, d = 3.2);
            }
        }
        
        // 挂墙孔
        translate([FRAME_W/2, FRAME_H - BEZEL - 5, -0.1])
            cylinder(h = BACK_T + 0.2, d = 6);
    }
}

// ========== 辅助 ==========
module rounded_rect(w, h, t, r) {
    linear_extrude(height = t)
    offset(r = r)
    square([w - r*2, h - r*2], center = false);
}

// ========== 装配预览 ==========
module assembly() {
    // 背板
    color("#333") back_panel();
    
    // 中框
    translate([0, 0, BACK_T]) color("#444") mid_frame();
    
    // 屏幕
    translate([BEZEL, (FRAME_H - SCREEN_H)/2, BACK_T + 1])
    color("#1a1a1a", 0.5) cube([SCREEN_W, SCREEN_H, SCREEN_T]);
    
    // PCB (60×40mm)
    translate([BEZEL + 10, (FRAME_H - SCREEN_H)/2 + 10, BACK_T + 1.25])
    color("green") cube([60, 40, 1.6]);
    
    // 电池
    bat_y = FRAME_H - BEZEL - BAT_H - 5;
    translate([BEZEL + 5, bat_y, BACK_T + 1.25])
    color("#888") cube([BAT_W, BAT_H, BAT_T]);
    
    // 喇叭 (3525)
    spk_x = FRAME_W/2 - SPK_W/2;
    translate([spk_x, 10, BACK_T + 1.25])
    color("#666") cube([SPK_W, SPK_H, SPK_T]);
    
    // C02 转接板 (41×22mm，屏幕右侧)
    translate([BEZEL + SCREEN_W - C02_W - 5, (FRAME_H - SCREEN_H)/2 + 5, BACK_T + 1.25])
    color("red", 0.7) cube([C02_W, C02_H, 1.6]);
    
    // 前面板
    translate([0, 0, BACK_T + INNER_T]) color("#555") front_panel();
}

// 导出：取消下面注释，F6渲染→F7导出STL
// front_panel();
// mid_frame();
// back_panel();
assembly();
