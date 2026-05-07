$fn = 80;

// "base" | "lid" | "assembly"
part = "assembly";

// -------- BOITIER --------
box_L = 105;
box_W = 65;
box_H = 28;

corner_r = 9;
wall     = 2.2;
floor_t  = 2.2;

// -------- COUVERCLE --------
lid_t = 2.6;
clearance = 0.8;
lip_h = 3.2;
lip_t = 1.0;

// -------- USB (rectangle) cÃ´tÃ© droit X+ --------
usb_w = 12;
usb_h = 7;
usb_y = 0;
usb_z = 14;

// -------- 2 TROUS RONDS (avant Y- et arriÃ¨re Y+) --------
front_d = 0;
back_d  = 0;

holes_x = +25;
front_z = 14;
back_z  = 14;

back_x_offset = 0;

// -------- SUPPORT PCB --------
pcb_x = 10;
pcb_y = 0;

hole_spacing = 24;   // distance entre les 2 trous du PCB
peg_d = 2.0;         // diamÃ¨tre plot
peg_h = 2.5;         // hauteur plot

// -------- OUTILS --------
module rr2d(L, W, r){
  r2 = min(r, min(L,W)/2 - 0.01);
  hull(){
    translate([ L/2 - r2,  W/2 - r2]) circle(r=r2);
    translate([-L/2 + r2,  W/2 - r2]) circle(r=r2);
    translate([ L/2 - r2, -W/2 + r2]) circle(r=r2);
    translate([-L/2 + r2, -W/2 + r2]) circle(r=r2);
  }
}

module rr3d(L, W, H, r){
  linear_extrude(height=H) rr2d(L,W,r);
}

// -------- SUPPORT PCB --------
module pcb_mount(){
  // seulement 2 petits plots
  translate([pcb_x - hole_spacing/2, pcb_y, floor_t])
    cylinder(d=peg_d, h=peg_h);

  translate([pcb_x + hole_spacing/2, pcb_y, floor_t])
    cylinder(d=peg_d, h=peg_h);
}

// -------- BASE (ouverte, intÃ©rieur vide) --------
module base_open(){
  difference(){
    rr3d(box_L, box_W, box_H, corner_r);

    // cavitÃ© intÃ©rieure ouverte par le haut
    translate([0,0,floor_t])
      rr3d(box_L-2*wall, box_W-2*wall,
           box_H-floor_t+0.3,
           max(corner_r-wall,1));

    // USB rectangle (cÃ´tÃ© droit X+)
    translate([ box_L/2 + 0.01, usb_y, usb_z ])
      cube([wall+6, usb_w, usb_h], center=true);

    // Trou rond face avant (Y-)
    translate([ holes_x, -(box_W/2 + 0.01), front_z ])
      rotate([90,0,0])
        cylinder(d=front_d, h=wall+6, center=true);

    // Trou rond face arriÃ¨re (Y+)
    translate([ holes_x + back_x_offset, +(box_W/2 + 0.01), back_z ])
      rotate([90,0,0])
        cylinder(d=back_d, h=wall+6, center=true);
  }

  // ajout support PCB
  pcb_mount();
}

// -------- COUVERCLE (plein + rebord) --------
module lid_full(){
  outer_L = box_L - 2*(wall + clearance);
  outer_W = box_W - 2*(wall + clearance);
  inner_L = box_L - 2*(wall + clearance + lip_t);
  inner_W = box_W - 2*(wall + clearance + lip_t);

  union(){
    rr3d(box_L, box_W, lid_t, corner_r);

    translate([0,0,lid_t])
      difference(){
        rr3d(outer_L, outer_W, lip_h,
             max(corner_r-(wall+clearance),1));

        translate([0,0,-0.05])
          rr3d(inner_L, inner_W, lip_h+0.1,
               max(corner_r-(wall+clearance+lip_t),1));
     
      }
  }
}

// -------- AFFICHAGE / EXPORT --------
if (part=="base") {
  base_open();
}
else if (part=="lid") {
  lid_full();
}
else {
  union(){
    base_open();
    translate([0, box_W + 10, 0]) lid_full();
  }
}