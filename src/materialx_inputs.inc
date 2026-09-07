// Standard Surface 1.0.1 input names, types and defaults (MaterialX 1.38.10).
R"mtlx({
  "base": {
    "type": "float",
    "default": 1.0
  },
  "base_color": {
    "type": "color3",
    "default": [
      0.8,
      0.8,
      0.8
    ]
  },
  "diffuse_roughness": {
    "type": "float",
    "default": 0.0
  },
  "metalness": {
    "type": "float",
    "default": 0.0
  },
  "specular": {
    "type": "float",
    "default": 1.0
  },
  "specular_color": {
    "type": "color3",
    "default": [
      1.0,
      1.0,
      1.0
    ]
  },
  "specular_roughness": {
    "type": "float",
    "default": 0.2
  },
  "specular_IOR": {
    "type": "float",
    "default": 1.5
  },
  "specular_anisotropy": {
    "type": "float",
    "default": 0.0
  },
  "specular_rotation": {
    "type": "float",
    "default": 0.0
  },
  "transmission": {
    "type": "float",
    "default": 0.0
  },
  "transmission_color": {
    "type": "color3",
    "default": [
      1.0,
      1.0,
      1.0
    ]
  },
  "transmission_depth": {
    "type": "float",
    "default": 0.0
  },
  "transmission_scatter": {
    "type": "color3",
    "default": [
      0.0,
      0.0,
      0.0
    ]
  },
  "transmission_scatter_anisotropy": {
    "type": "float",
    "default": 0.0
  },
  "transmission_dispersion": {
    "type": "float",
    "default": 0.0
  },
  "transmission_extra_roughness": {
    "type": "float",
    "default": 0.0
  },
  "subsurface": {
    "type": "float",
    "default": 0.0
  },
  "subsurface_color": {
    "type": "color3",
    "default": [
      1.0,
      1.0,
      1.0
    ]
  },
  "subsurface_radius": {
    "type": "color3",
    "default": [
      1.0,
      1.0,
      1.0
    ]
  },
  "subsurface_scale": {
    "type": "float",
    "default": 1.0
  },
  "subsurface_anisotropy": {
    "type": "float",
    "default": 0.0
  },
  "sheen": {
    "type": "float",
    "default": 0.0
  },
  "sheen_color": {
    "type": "color3",
    "default": [
      1.0,
      1.0,
      1.0
    ]
  },
  "sheen_roughness": {
    "type": "float",
    "default": 0.3
  },
  "coat": {
    "type": "float",
    "default": 0.0
  },
  "coat_color": {
    "type": "color3",
    "default": [
      1.0,
      1.0,
      1.0
    ]
  },
  "coat_roughness": {
    "type": "float",
    "default": 0.1
  },
  "coat_anisotropy": {
    "type": "float",
    "default": 0.0
  },
  "coat_rotation": {
    "type": "float",
    "default": 0.0
  },
  "coat_IOR": {
    "type": "float",
    "default": 1.5
  },
  "coat_normal": {
    "type": "vector3"
  },
  "coat_affect_color": {
    "type": "float",
    "default": 0.0
  },
  "coat_affect_roughness": {
    "type": "float",
    "default": 0.0
  },
  "thin_film_thickness": {
    "type": "float",
    "default": 0.0
  },
  "thin_film_IOR": {
    "type": "float",
    "default": 1.5
  },
  "emission": {
    "type": "float",
    "default": 0.0
  },
  "emission_color": {
    "type": "color3",
    "default": [
      1.0,
      1.0,
      1.0
    ]
  },
  "opacity": {
    "type": "color3",
    "default": [
      1.0,
      1.0,
      1.0
    ]
  },
  "thin_walled": {
    "type": "boolean",
    "default": false
  },
  "normal": {
    "type": "vector3"
  },
  "tangent": {
    "type": "vector3"
  }
})mtlx"
