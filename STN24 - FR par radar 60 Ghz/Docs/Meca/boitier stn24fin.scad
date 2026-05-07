//$fn = 80;

// -------- DIMENSIONS COMMUNES --------
box_L    = 65;
box_W    = 100;
box_H    = 28;
corner_r = 9;
wall     = 2.2;
floor_t  = 2.2;
spacing  = 10;

// -------- USB-C XIAO (face avant Y-) --------
usb_w = 12;
usb_h = 7;
usb_x = 0;
usb_z = 14;

// -------- DIMENSIONS PCB --------
pcb_L      = 34;
pcb_W      = 54;
pcb_corner = 2.5;
hole_dx    = pcb_L - 2 * pcb_corner;
hole_dy    = pcb_W - 2 * pcb_corner;
pcb_x      = 0;
pcb_y      = 0;
peg_d      = 1.8;
peg_h      = 4.0;

// -------- COUVERCLE --------
lid_t        = 2.6;
clearance    = 0.8;
lip_h        = 3.2;
lip_t        = 1.0;
sensor_win_L = 20;
sensor_win_W = 20;
sensor_win_x = 0;                        // centré en X
sensor_win_y = -(box_W/2 - 30- wall);  // décalé vers Y- (côté USB)

// -------- OUTILS --------
module rr2d(L, W, r){
  r2 = min(r, min(L,W)/2 - 0.01);
  hull(){
    translate([ L/2-r2,  W/2-r2]) circle(r=r2);
    translate([-L/2+r2,  W/2-r2]) circle(r=r2);
    translate([ L/2-r2, -W/2+r2]) circle(r=r2);
    translate([-L/2+r2, -W/2+r2]) circle(r=r2);
  }
}
module rr3d(L, W, H, r){
  linear_extrude(height=H) rr2d(L,W,r);
}

// -------- SUPPORTS PCB (4 coins) --------
module pcb_mount(){
  positions = [
    [ hole_dx/2,  hole_dy/2],
    [-hole_dx/2,  hole_dy/2],
    [ hole_dx/2, -hole_dy/2],
    [-hole_dx/2, -hole_dy/2]
  ];
  for (p = positions){
    translate([pcb_x + p[0], pcb_y + p[1], floor_t])
      cylinder(d=peg_d, h=peg_h);
  }
}

// -------- BASE --------
module base_open(){
  difference(){
    rr3d(box_L, box_W, box_H, corner_r);
    translate([0,0,floor_t])
      rr3d(box_L-2*wall, box_W-2*wall,
           box_H-floor_t+0.3,
           max(corner_r-wall,1));
    // Trou USB-C (face avant Y-)
    translate([usb_x, -box_W/2 - 0.01, usb_z])
      cube([usb_w, wall+6, usb_h], center=true);
  }
  pcb_mount();
}

// -------- COUVERCLE --------
module lid_full(){
  outer_L = box_L - 2*(wall + clearance);
  outer_W = box_W - 2*(wall + clearance);
  inner_L = box_L - 2*(wall + clearance + lip_t);
  inner_W = box_W - 2*(wall + clearance + lip_t);

  difference(){
    union(){
      rr3d(box_L, box_W, lid_t, corner_r);
      translate([0, 0, -lip_h])
        difference(){
          rr3d(outer_L, outer_W, lip_h,
               max(corner_r-(wall+clearance),1));
          translate([0, 0, -0.05])
            rr3d(inner_L, inner_W, lip_h+0.1,
                 max(corner_r-(wall+clearance+lip_t),1));
        }
    }
    // Fenêtre radar C1001 côté Y- (même côté que USB)
    translate([sensor_win_x, sensor_win_y, -lid_t/2])
      cube([sensor_win_L, sensor_win_W, lid_t+1], center=true);
  }
}

// -------- RENDU FINAL : BASE + COUVERCLE CÔTE À CÔTE --------
translate([0, 0, 0])
  base_open();

translate([box_L + spacing, 0, lid_t])
  rotate([180, 0, 0])
    lid_full();