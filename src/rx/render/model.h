#ifndef RX_RENDER_MODEL_H
#define RX_RENDER_MODEL_H
#include "rx/render/frontend/material.h"

#include "rx/model/loader.h"
#include "rx/model/animation.h"

#include "rx/math/mat4x4.h"
#include "rx/math/aabb.h"

namespace rx::render {

namespace frontend {
  struct interface;
  struct technique;
  struct buffer;
  struct target;
}

struct immediate3D;
struct ibl;

struct model {
  model(frontend::interface* _frontend);
  ~model();

  struct mesh {
    rx_size offset;
    rx_size count;
    rx_size material;
    math::aabb bounds;
  };

  void animate(rx_size _index, bool _loop);

  void update(rx_f32 _delta_time);

  void render(frontend::target* _target, const math::mat4x4f& _model,
    const math::mat4x4f& _view, const math::mat4x4f& _projection);

  void render_normals(const math::mat4x4f& _world, render::immediate3D* _immediate);
  void render_skeleton(const math::mat4x4f& _world, render::immediate3D* _immediate);

  bool load(const string& _file_name);

  const vector<mesh>& opaque_meshes() const &;
  const vector<mesh>& transparent_meshes() const &;

private:
  frontend::interface* m_frontend;
  frontend::technique* m_technique;
  frontend::buffer* m_buffer;
  vector<frontend::material> m_materials;
  vector<mesh> m_opaque_meshes;
  vector<mesh> m_transparent_meshes;
  rx::model::loader m_model;
  optional<rx::model::animation> m_animation;
  math::aabb m_aabb;
};

inline const vector<model::mesh>& model::opaque_meshes() const & {
  return m_opaque_meshes;
}

inline const vector<model::mesh>& model::transparent_meshes() const & {
  return m_transparent_meshes;
}

} // namespace rx::render

#endif // RX_RENDER_MODEL_H
