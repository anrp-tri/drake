#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <vtkActor.h>
#include <vtkAutoInit.h>
#include <vtkCommand.h>
#include <vtkImageExport.h>
#include <vtkLight.h>
#include <vtkNew.h>
#include <vtkPolyDataAlgorithm.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkShaderProgram.h>
#include <vtkSmartPointer.h>
#include <vtkWindowToImageFilter.h>

#include "drake/common/drake_copyable.h"
#include "drake/common/drake_export.h"
#include "drake/geometry/render/render_engine.h"
#include "drake/geometry/render/render_label.h"
#include "drake/geometry/render/render_material.h"
#include "drake/geometry/render_vtk/render_engine_vtk_params.h"

#ifndef DRAKE_DOXYGEN_CXX
// This, and the ModuleInitVtkRenderingOpenGL2, provide the basis for enabling
// VTK's OpenGL2 infrastructure.
VTK_AUTOINIT_DECLARE(vtkRenderingOpenGL2)
#endif

namespace drake {
namespace geometry {
namespace render_vtk {
namespace internal {

#ifndef DRAKE_DOXYGEN_CXX
struct ModuleInitVtkRenderingOpenGL2 {
  ModuleInitVtkRenderingOpenGL2(){
    VTK_AUTOINIT_CONSTRUCT(vtkRenderingOpenGL2)
  }
};
#endif

// A callback class for setting uniform variables used in shader programs,
// namely z_near and z_far, when vtkCommand::UpdateShaderEvent is caught.
// See also shaders::kDepthFS, this is where the variables are used.
// For the detail of VTK's callback mechanism, please refer to:
// https://www.vtk.org/doc/nightly/html/classvtkCommand.html#details
class DRAKE_NO_EXPORT ShaderCallback : public vtkCommand {
 public:
  DRAKE_DEFAULT_COPY_AND_MOVE_AND_ASSIGN(ShaderCallback);

  ShaderCallback();

  static ShaderCallback* New() { return new ShaderCallback; }

  // NOLINTNEXTLINE(runtime/int): To match pre-existing APIs.
  void Execute(vtkObject*, unsigned long, void* callback_object) override {
    vtkShaderProgram* program =
        reinterpret_cast<vtkShaderProgram*>(callback_object);
    program->SetUniformf("z_near", z_near_);
    program->SetUniformf("z_far", z_far_);
  }

  void set_z_near(float z_near) {
    z_near_ = z_near;
  }

  void set_z_far(float z_far) {
    z_far_ = z_far;
  }

 private:
  float z_near_{0.f};
  float z_far_{0.f};
};

// Not for external use, RenderEngineVtk uses this to index pipelines_ member.
// Do not change, remove, or add any values.
enum ImageType {
  kColor = 0,
  kLabel = 1,
  kDepth = 2,
};

/* See documentation of MakeRenderEngineVtk().  */
class DRAKE_NO_EXPORT RenderEngineVtk : public render::RenderEngine,
                                        private ModuleInitVtkRenderingOpenGL2 {
 public:
  /* @name Does not allow copy, move, or assignment  */
  //@{

  // Note: the copy constructor operator is actually protected to serve as the
  // basis for implementing the DoClone() method.
  RenderEngineVtk& operator=(const RenderEngineVtk&) = delete;
  RenderEngineVtk(RenderEngineVtk&&) = delete;
  RenderEngineVtk& operator=(RenderEngineVtk&&) = delete;
  //@}}

  /* Constructs the render engine from the given `parameters`.

   When one of the optional parameters is omitted, the constructed value will be
   as documented elsewhere in @ref render_engine_vtk_properties "this class".
  */
  explicit RenderEngineVtk(
      const RenderEngineVtkParams& parameters = RenderEngineVtkParams());

  /* @see RenderEngine::UpdateViewpoint().  */
  void UpdateViewpoint(const math::RigidTransformd& X_WR) override;

  /* @name    Shape reification  */
  //@{
  using RenderEngine::ImplementGeometry;
  void ImplementGeometry(const Box& box, void* user_data) override;
  void ImplementGeometry(const Capsule& capsule, void* user_data) override;
  void ImplementGeometry(const Convex& convex, void* user_data) override;
  void ImplementGeometry(const Cylinder& cylinder, void* user_data) override;
  void ImplementGeometry(const Ellipsoid& ellipsoid, void* user_data) override;
  void ImplementGeometry(const HalfSpace& half_space, void* user_data) override;
  void ImplementGeometry(const Mesh& mesh, void* user_data) override;
  void ImplementGeometry(const Sphere& sphere, void* user_data) override;
  //@}

  /* @name    Access the default properties

   Provides access to the default values this instance of the render engine is
   using. These values must be set at construction.  */
  //@{

  Eigen::Vector4d default_diffuse() const {
    return default_diffuse_.rgba();
  }

  using render::RenderEngine::default_render_label;
  //@}

 protected:
  /* Returns all actors registered with the engine, keyed by the SceneGraph
   GeometryId. Each GeometryId maps to a triple of actors: color, depth, and
   label. */
  const std::unordered_map<GeometryId,
                           std::array<vtkSmartPointer<vtkActor>, 3>>&
  actors() const {
    return actors_;
  }

  /* Copy constructor for the purpose of cloning. */
  RenderEngineVtk(const RenderEngineVtk& other);

  /* The rendering pipeline for a single image type (color, depth, or label). */
  struct RenderingPipeline {
    vtkNew<vtkRenderer> renderer;
    vtkNew<vtkRenderWindow> window;
    vtkNew<vtkWindowToImageFilter> filter;
    vtkNew<vtkImageExport> exporter;
  };

  /* Configures the VTK model to reflect the given `camera`, this includes
   render size camera intrinsics, visible windows, etc. If `show_window` is set
   to true, a named VTK window will be displayed. */
  void UpdateWindow(const render::RenderCameraCore& camera, bool show_window,
                    const RenderingPipeline& p, const char* name) const;

  /* Variant of configuring the VTK model (see previous function) that *also*
   configures the depth range. */
  void UpdateWindow(const render::DepthRenderCamera& camera,
                    const RenderingPipeline& p) const;

  /* Updates VTK rendering related objects including vtkRenderWindow,
   vtkWindowToImageFilter and vtkImageExporter, so that VTK reflects
   vtkActors' pose update for rendering. */
  static void PerformVtkUpdate(const RenderingPipeline& p);

  /* Provides access to the private data member pipelines_ by returning a
   mutable RenderingPipeline reference. Only image types in ImageType enum are
   valid. */
  RenderingPipeline& get_mutable_pipeline(ImageType image_type) const;

  /* A package of data required to register a visual geometry. This is passed as
   the void* user_data in the `ShapeReifier::ImplementGeometry()` methods. */
  struct RegistrationData {
    const PerceptionProperties& properties;
    const math::RigidTransformd& X_WG;
    const GeometryId id;
    bool accepted{true};
  };

  // @see RenderEngine::DoRegisterVisual().
  bool DoRegisterVisual(GeometryId id, const Shape& shape,
                        const PerceptionProperties& properties,
                        const math::RigidTransformd& X_WG) override;

  // @see RenderEngine::DoUpdateVisualPose().
  void DoUpdateVisualPose(GeometryId id,
                          const math::RigidTransformd& X_WG) override;

  // @see RenderEngine::DoRemoveGeometry().
  bool DoRemoveGeometry(GeometryId id) override;

  // @see RenderEngine::DoClone().
  std::unique_ptr<RenderEngine> DoClone() const override;

  // @see RenderEngine::DoRenderColorImage().
  void DoRenderColorImage(
      const render::ColorRenderCamera& camera,
      systems::sensors::ImageRgba8U* color_image_out) const override;

  // @see RenderEngine::DoRenderDepthImage().
  void DoRenderDepthImage(
      const render::DepthRenderCamera& render_camera,
      systems::sensors::ImageDepth32F* depth_image_out) const override;

  // @see RenderEngine::DoRenderLabelImage().
  void DoRenderLabelImage(
      const render::ColorRenderCamera& camera,
      systems::sensors::ImageLabel16I* label_image_out) const override;

  // Common interface for loading an obj file -- used for both mesh and convex
  // shapes. If `file_name` is of an unsupported type, no geometry will be
  // added.
  void ImplementObj(const std::string& file_name, double scale,
                    void* user_data);

 private:
  // Initializes the VTK pipelines.
  void InitializePipelines();

  // Performs the common setup for all shape types.
  void ImplementPolyData(vtkPolyDataAlgorithm* source,
                         const geometry::internal::RenderMaterial& material,
                         void* user_data);

  void SetDefaultLightPosition(const Vector3<double>& X_DL) override;

  // Three pipelines: rgb, depth, and label.
  static constexpr int kNumPipelines = 3;

  std::array<std::unique_ptr<RenderingPipeline>, kNumPipelines> pipelines_;

  vtkNew<vtkLight> light_;

  // By design, all of the geometry is shared across clones of the render
  // engine. This is predicated upon the idea that the geometry is *not*
  // deformable and does *not* depend on the system's pose information.
  // (If there is deformable geometry, it will have to be handled differently.)
  // Having "shared geometry" means having shared vtkPolyDataAlgorithm and
  // vtkOpenGLPolyDataMapper instances. The shader callback gets registered to
  // the *mapper* instances, so they all, implicitly, share the same callback.
  // Making this member static facilitates that but it does preclude the
  // possibility of simultaneous renderings with different uniform parameters.
  // Currently, this doesn't happen because drake isn't particularly thread safe
  // (or executed in such a context). However, this renderer will need some
  // formal thread safe mechanism so that it doesn't rely on that in the future.
  // TODO(SeanCurtis-TRI): This is not threadsafe; investigate mechanisms to
  // prevent undesirable behaviors if used in multi-threaded application.
  static vtkNew<ShaderCallback> uniform_setting_callback_;

  // Obnoxious bright orange.
  Rgba default_diffuse_{0.9, 0.45, 0.1, 1.0};

  // The color to clear the color buffer to.
  systems::sensors::ColorD default_clear_color_;

  // The collection of per-geometry actors (one actor per pipeline (color,
  // depth, and label) keyed by the geometry's GeometryId.
  std::unordered_map<GeometryId, std::array<vtkSmartPointer<vtkActor>, 3>>
      actors_;
};

}  // namespace internal
}  // namespace render_vtk
}  // namespace geometry
}  // namespace drake
