/*
 * Laigter: an automatic map generator for lighting effects.
 * Copyright (C) 2019  Pablo Ivan Fonovich
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 * Contact: azagaya.games@gmail.com
 */

#include "open_gl_widget.h"

#include <math.h>

#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QOpenGLFramebufferObject>
#include <QOpenGLVersionProfile>
#include <QOpenGLVertexArrayObject>
#include <QPainter>

OpenGlWidget::OpenGlWidget(QWidget *parent)
{
  m_zoom = 1.0;
  laigter = QImage(":/images/laigter_texture.png");
  ambientColor = QColor("white");
  ambientIntensity = 0.8;
  lightPosition = QVector3D(0.7, 0.7, 0.3);
  m_light = true;
  m_parallax = false;
  parallax_height = 0.03;
  // processor->set_tile_x(false);
  // processor->set_tile_y(false);
  m_pixelated = false;
  m_toon = false;
  lightSelected = false;
  currentLight = new LightSource();
  currentLight->set_light_position(lightPosition);
  QColor c;
  c.setRgbF(0.0, 1, 0.7);
  currentLight->set_diffuse_color(c);
  currentLight->set_specular_color(c);
  currentLight->set_specular_scatter(32);
  currentLight->set_diffuse_intensity(0.6);
  currentLight->set_specular_intensity(0.6);
  lightList.append(currentLight);
  currentLightList = &lightList;
  backgroundColor.setRgbF(0.2, 0.2, 0.3);
  pixelSize = 3;
  refreshTimer.setInterval(1.0 / 30.0 * 1000.0);
  refreshTimer.setSingleShot(false);
  connect(&refreshTimer, SIGNAL(timeout()), this, SLOT(force_update()));
  refreshTimer.start();
  need_to_update = true;
  export_render = false;
  exportFullView = false;
  addLight = false;
  this->setMouseTracking(true);
  viewmode = 0;
  sample_light_list_used = true;
  oldPos = QPoint(0, 0);
  currentBrush = nullptr;
}

void OpenGlWidget::initializeGL()
{
  initializeOpenGLFunctions();
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_BLEND);
  glClearColor(
      backgroundColor.redF() * ambientColor.redF() * ambientIntensity,
      backgroundColor.greenF() * ambientColor.greenF() * ambientIntensity,
      backgroundColor.blueF() * ambientColor.blueF() * ambientIntensity, 1.0);

  setUpdateBehavior(QOpenGLWidget::PartialUpdate);
  m_program.create();
  m_program.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                    ":/shaders/vshader.glsl");
  m_program.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                    ":/shaders/fshader.glsl");
  m_program.link();
  lightProgram.create();
  lightProgram.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                       ":/shaders/lvshader.glsl");
  lightProgram.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                       ":/shaders/lfshader.glsl");
  cursorProgram.create();
  cursorProgram.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                        ":/shaders/lvshader.glsl");
  cursorProgram.addShaderFromSourceFile(
      QOpenGLShader::Fragment, ":/shaders/cursor_fragment_shader.glsl");

  // set up vertex data (and buffer(s)) and configure vertex attributes
  // ------------------------------------------------------------------
  float vertices[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, // bot left
      1.0f, -1.0f, 0.0f, 1.0f, 1.0f,  // bot right
      1.0f, 1.0f, 0.0f, 1.0f, 0.0f,   // top right
      -1.0f, 1.0f, 0.0f, 0.0f, 0.0f   // top left
  };

  // bind the Vertex Array Object first, then bind and set vertex buffer(s),
  // and then configure vertex attributes(s).

  VAO.bind();
  VBO.create();
  VBO.bind();
  VBO.allocate(vertices, sizeof(vertices));
  m_program.setAttributeBuffer("aPos", GL_FLOAT, 0, 3, 5 * sizeof(float));
  m_program.enableAttributeArray("aPos");
  m_program.setAttributeBuffer("aTexCoord", GL_FLOAT, (3 * sizeof(float)), 2,
                               5 * sizeof(float));
  m_program.enableAttributeArray("aTexCoord");
  VAO.release();
  VBO.release();
  VBO.bind();
  lightProgram.setAttributeBuffer("aPos", GL_FLOAT, 0, 3, 5 * sizeof(float));
  lightProgram.enableAttributeArray("aPos");
  lightProgram.setAttributeBuffer("aTexCoord", GL_FLOAT, 3 * sizeof(float), 2,
                                  5 * sizeof(float));
  lightProgram.enableAttributeArray("aTexCoord");
  lightVAO.release();
  VBO.release();
  initialized();
}

void OpenGlWidget::loadTextures()
{
  processor = processorList.at(0);
  m_texture = new QOpenGLTexture(m_image);
  m_parallaxTexture = new QOpenGLTexture(parallaxMap);
  m_specularTexture = new QOpenGLTexture(specularMap);
  m_normalTexture = new QOpenGLTexture(normalMap);
  m_occlusionTexture = new QOpenGLTexture(occlusionMap);
  laigterTexture = new QOpenGLTexture(laigter);
  brushTexture = new QOpenGLTexture(laigter);
}

void OpenGlWidget::paintGL()
{
  if (need_to_update)
  {
    need_to_update = false;
    update_scene();
  }

  if (export_render)
  {
    export_render = false;
    renderedPreview = calculate_preview(m_fullPreview);
  }
}

void OpenGlWidget::update() { QOpenGLWidget::update(); }

void OpenGlWidget::force_update()
{
  if (need_to_update)
    update();
}

void OpenGlWidget::update_scene()
{
  static float rotation = 0;
  glClearColor(
      backgroundColor.redF() * ambientColor.redF() * ambientIntensity,
      backgroundColor.greenF() * ambientColor.greenF() * ambientIntensity,
      backgroundColor.blueF() * ambientColor.blueF() * ambientIntensity, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);

  QVector3D color;
  double r, g, b;
  GLfloat bkColor[4];
  glGetFloatv(GL_COLOR_CLEAR_VALUE, bkColor);

  int i1 = m_pixelated ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
  int i2 = m_pixelated ? GL_NEAREST : GL_LINEAR;

  QMatrix4x4 transform;

  m_program.bind();
  m_program.setUniformValue("view_mode", viewmode);
  m_program.setUniformValue("pixelated", m_pixelated);
  backgroundColor.getRgbF(&r, &g, &b, nullptr);
  color = QVector3D(bkColor[0], bkColor[1], bkColor[2]);
  m_program.setUniformValue("outlineColor", color);
  m_program.setUniformValue("toon", m_toon);
  m_program.setUniformValue("viewPos", QVector3D(0, 0, 1));
  m_program.setUniformValue("height_scale", parallax_height);
  m_program.setUniformValue("blend_factor", static_cast<float>(blend_factor/100.0));

  apply_light_params();
  foreach (ImageProcessor *processor, processorList)
  {
    if (processor->get_current_frame()->get_image(TextureTypes::Diffuse,
                                                  &m_image))
      setImage(&m_image);

    if (processor->get_current_frame()->get_image(TextureTypes::Normal,
                                                  &normalMap))
      setNormalMap(&normalMap);

    if (processor->get_current_frame()->get_image(TextureTypes::Specular,
                                                  &specularMap))
      setSpecularMap(&specularMap);

    if (processor->get_current_frame()->get_image(TextureTypes::Parallax,
                                                  &parallaxMap))
      setParallaxMap(&parallaxMap);

    if (processor->get_current_frame()->get_image(TextureTypes::Occlussion,
                                                  &occlusionMap))
      setOcclusionMap(&occlusionMap);

    transform.setToIdentity();
    QVector3D texPos = *processor->get_position();
    if (processor->get_tile_x())
      texPos.setX(0);

    if (processor->get_tile_y())
      texPos.setY(0);

    transform.translate(texPos);
    float scaleX = !processor->get_tile_x() ? sx : 1;
    float scaleY = !processor->get_tile_y() ? sy : 1;
    transform.scale(scaleX, scaleY, 1);
    float zoomX = !processor->get_tile_x() ? processor->get_zoom() : 1;
    float zoomY = !processor->get_tile_y() ? processor->get_zoom() : 1;
    transform.scale(zoomX, zoomY, 1);

    transform.rotate(180.0 * rotation / 3.1415, QVector3D(0, 0, 1));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, i1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, i2);

    /* Start first pass */

    VAO.bind();

    if (processor->get_tile_x() || processor->get_tile_y())
    {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    else
    {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                      GL_CLAMP_TO_BORDER);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                      GL_CLAMP_TO_BORDER);
    }

    glActiveTexture(GL_TEXTURE0);

    m_program.setUniformValue("transform", transform);
    m_program.setUniformValue("pixelsX", pixelsX);
    m_program.setUniformValue("pixelsY", pixelsY);
    m_program.setUniformValue("pixelSize", pixelSize);
    m_program.setUniformValue("selected", processor->get_selected());
    m_program.setUniformValue("textureScale", processor->get_zoom());
    m_program.setUniformValue("rotation_angle", rotation);
    scaleX = processor->get_tile_x() ? sx : 1;
    scaleY = processor->get_tile_y() ? sy : 1;
    zoomX = processor->get_tile_x() ? processor->get_zoom() : 1;
    zoomY = processor->get_tile_y() ? processor->get_zoom() : 1;
    m_program.setUniformValue(
        "ratio", QVector2D(1 / scaleX / zoomX, 1 / scaleY / zoomY));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, i1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, i2);

    m_texture->bind(0);
    m_program.setUniformValue("diffuse", 0);
    m_normalTexture->bind(1);
    m_program.setUniformValue("normalMap", 1);
    m_parallaxTexture->bind(2);
    m_program.setUniformValue("parallaxMap", 2);
    m_specularTexture->bind(3);
    m_program.setUniformValue("specularMap", 3);
    m_occlusionTexture->bind(4);
    m_program.setUniformValue("occlussionMap", 4);
    m_program.setUniformValue("parallax", processor->get_is_parallax() &&
                                              viewmode == Preview);
    // m_texture->bind(0);
    glDrawArrays(GL_QUADS, 0, 4);
    /* Render light texture */
  }

  m_program.release();
  QList<LightSource *> currentLightList;
  if (sample_light_list_used)
    currentLightList = *sampleLightList;
  else
  {
    foreach (ImageProcessor *processor, processorList)
    {
      foreach (LightSource *l, *processor->get_light_list_ptr())
        currentLightList.append(l);
    }
  }

  if (currentLightList.count() > 0)
  {
    foreach (LightSource *light, currentLightList)
    {
      float x = static_cast<float>(laigter.width()) / width();
      float y = static_cast<float>(laigter.height()) / height();
      transform.setToIdentity();
      transform.translate(light->get_light_position());
      transform.scale(static_cast<float>(0.3) * x,
                      static_cast<float>(0.3) * y, 1);
      lightProgram.bind();
      lightVAO.bind();
      lightProgram.setUniformValue("transform", transform);
      laigterTexture->bind(0);

      if (m_light)
      {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        GL_LINEAR);
        glActiveTexture(GL_TEXTURE0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_BORDER);
        lightProgram.setUniformValue("texture", 0);
        lightProgram.setUniformValue("pixelSize", 3.0 / x, 3.0 / y);
        lightProgram.setUniformValue("selected", currentLight == light);
        light->get_diffuse_color().getRgbF(&r, &g, &b, nullptr);
        color = QVector3D(r, g, b);
        lightProgram.setUniformValue("lightColor", color);
        glDrawArrays(GL_QUADS, 0, 4);
      }
    }
    lightProgram.release();
  }
  /* Render brush cursor */

  if (currentBrush && currentBrush->get_selected())
  {
    setCursor(Qt::BlankCursor);
    brushTexture->destroy();
    brushTexture->create();
    brushTexture->setData(currentBrush->getBrushSprite());
    float x = static_cast<float>(brushTexture->width()) / width() *
              processor->get_zoom();
    float y = static_cast<float>(brushTexture->height()) / height() *
              processor->get_zoom();
    QPoint cursor = mapFromGlobal(QCursor::pos());
    transform.setToIdentity();
    transform.translate(2.0 * cursor.x() / width() - 1,
                        -2.0 * cursor.y() / height() + 1);
    transform.scale(x, y, 1);
    cursorProgram.bind();
    lightVAO.bind();
    cursorProgram.setUniformValue("transform", transform);
    brushTexture->bind(0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glActiveTexture(GL_TEXTURE0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    cursorProgram.setUniformValue("texture", 0);
    cursorProgram.setUniformValue("scale", processor->get_zoom());
    cursorProgram.setUniformValue("pixelSize", 1.0 / brushTexture->width(),
                                  1.0 / brushTexture->height());
    cursorProgram.setUniformValue("pixelated", m_pixelated);
    color = QVector3D(0.2, 0.2, 0.2);
    glDrawArrays(GL_QUADS, 0, 4);
    cursorProgram.release();
  }
  else
    setCursor(Qt::ArrowCursor);
}

void OpenGlWidget::resizeGL(int w, int h)
{
  sx = (float)m_image.width() / w;
  sy = (float)m_image.height() / h;
  need_to_update = true;
}

void OpenGlWidget::setImage(QImage *image)
{
  if (m_texture->isCreated())
    m_texture->destroy();

  m_texture->create();
  m_texture->setData(*image);
  sx = (float)image->width() / width();
  sy = (float)image->height() / height();
  pixelsX = image->width();
  pixelsY = image->height();
}

void OpenGlWidget::setNormalMap(QImage *image)
{
  m_normalTexture->destroy();
  if (m_normalTexture->create())
    m_normalTexture->setData(*image);
}

void OpenGlWidget::setOcclusionMap(QImage *image)
{
  m_occlusionTexture->destroy();
  m_occlusionTexture->create();
  m_occlusionTexture->setData(*image);
}

void OpenGlWidget::setParallaxMap(QImage *image)
{
  m_parallaxTexture->destroy();
  m_parallaxTexture->create();
  m_parallaxTexture->setData(*image);
}

void OpenGlWidget::setSpecularMap(QImage *image)
{
  m_specularTexture->destroy();
  if (m_specularTexture->create())
    m_specularTexture->setData(*image);
}

void OpenGlWidget::setZoom(float zoom)
{
  processor->set_zoom(zoom);
  need_to_update = true;
}

void OpenGlWidget::setTileX(bool x)
{
  foreach (ImageProcessor *p, get_all_selected_processors())
    p->set_tile_x(x);

  need_to_update = true;
}

void OpenGlWidget::setTileY(bool y)
{
  foreach (ImageProcessor *p, get_all_selected_processors())
    p->set_tile_y(y);

  need_to_update = true;
}

void OpenGlWidget::setParallax(bool p)
{
  foreach (ImageProcessor *processor, get_all_selected_processors())
    processor->set_is_parallax(p);
  need_to_update = true;
}

void OpenGlWidget::wheelEvent(QWheelEvent *event)
{
  QPoint degree = event->angleDelta() / 8;
  foreach (ImageProcessor *processor, get_all_selected_processors())
  {
    if (!degree.isNull() && degree.y() != 0)
    {
      QPoint step = degree / qAbs(degree.y());
      processor->set_zoom(step.y() > 0
                              ? processor->get_zoom() * 1.1 * step.y()
                              : -processor->get_zoom() * 0.9 * step.y());
    }
  }
  need_to_update = true;
}

void OpenGlWidget::resetZoom()
{
  setZoom(1.0);
  processor->set_position(QVector3D(0, 0, 0));
}

void OpenGlWidget::fitZoom()
{
  float x, y, s;
  x = (float)m_image.width() / width();
  y = (float)m_image.height() / height();
  s = qMax(x, y);
  setZoom(1 / s);
  processor->set_position(QVector3D(0, 0, 0));
}

float OpenGlWidget::getZoom() { return processor->get_zoom(); }

void OpenGlWidget::mousePressEvent(QMouseEvent *event)
{
  if (currentBrush && currentBrush->get_selected())
  {
    QPoint tpos;
    if (!processor->get_tile_x())
      tpos.setX((event->localPos().x() -
                 ((processor->get_position()->x() + 1) * width() -
                  processor->get_texture()->size().width() *
                      processor->get_zoom()) *
                     0.5) /
                processor->get_zoom());
    else
      tpos.setX((event->localPos().x()) / processor->get_zoom());
    if (!processor->get_tile_y())
      tpos.setY((event->localPos().y() -
                 ((-processor->get_position()->y() + 1) * height() -
                  processor->get_texture()->size().height() *
                      processor->get_zoom()) *
                     0.5) /
                processor->get_zoom());
    else
      tpos.setY((event->localPos().y()) / processor->get_zoom());

    oldPos = tpos;
    currentBrush->mousePress(tpos);
  }

  float lightWidth = (float)laigter.width() / width() *
                     0.3; // en paintgl la imagen la escalamos por 0.3
  float lightHeight = (float)laigter.height() / height() * 0.3;
  float mouseX = (float)event->localPos().x() / width() * 2 - 1;
  float mouseY = -(float)event->localPos().y() / height() * 2 + 1;

  if (event->buttons() & (Qt::LeftButton | Qt::MidButton))
  {
    if (addLight)
    {
      set_add_light(true);
      return;
    }
    /* Loops for selecting textues */
    if (QApplication::keyboardModifiers() != Qt::CTRL)
      set_all_processors_selected(false);

    bool selected = false;
    /* We first check if we selected a light, and if not, we scan if we
         * selected a texture */
    if (sample_light_list_used)
    {
      foreach (LightSource *light, *sampleLightList)
      {
        lightPosition = light->get_light_position();
        if (qAbs(mouseX - lightPosition.x()) < lightWidth &&
            qAbs(mouseY - lightPosition.y()) < lightHeight && m_light)
        {
          lightSelected = true;
          select_light(light);
          break;
        }
      }
    }
    else
    {
      foreach (ImageProcessor *p, processorList)
      {
        currentLightList = p->get_light_list_ptr();
        foreach (LightSource *light, *currentLightList)
        {
          lightPosition = light->get_light_position();
          if (qAbs(mouseX - lightPosition.x()) < lightWidth &&
              qAbs(mouseY - lightPosition.y()) < lightHeight &&
              m_light)
          {
            lightSelected = true;
            select_light(light);
            break;
          }
        }
        if (lightSelected)
          break;
      }
    }

    if (!lightSelected)
    {
      set_enabled_light_controls(false);
      for (int i = processorList.count() - 1; i >= 0; i--)
      {
        ImageProcessor *processor = processorList.at(i);
        processor->set_offset(QVector3D(mouseX, mouseY, 0) -
                              *processor->get_position());
        float w = processor->get_tile_x()
                      ? 2
                      : processor->get_zoom() *
                            processor->texture.width() / width();
        float h = processor->get_tile_y()
                      ? 2
                      : processor->get_zoom() *
                            processor->texture.height() / height();
        if (qAbs(mouseX - processor->get_position()->x()) < w &&
            qAbs(mouseY - processor->get_position()->y()) < h &&
            not selected)
        {
          set_processor_selected(processor, true);
          selected = true;
        }
      }
    }
    else
      set_enabled_light_controls(true);
  }
  else if (event->buttons() & Qt::RightButton)
  {
    int count;
    if (sample_light_list_used)
    {
      count = sampleLightList->count();
      if (addLight && sampleLightList->count() > 0)
      {
        foreach (LightSource *light, *sampleLightList)
        {
          if (light == currentLight)
            continue;

          lightPosition = light->get_light_position();
          if (qAbs(mouseX - lightPosition.x()) < lightWidth &&
              qAbs(mouseY - lightPosition.y()) < lightHeight &&
              m_light)
          {
            remove_light(light);
            break;
          }
        }
        if (count == sampleLightList->count())
          stopAddingLight();
      }
    }
    else
    {
      bool removed = false;
      foreach (ImageProcessor *p, processorList)
      {
        currentLightList = p->get_light_list_ptr();
        count = currentLightList->count();
        foreach (LightSource *light, *currentLightList)
        {
          if (light == currentLight)
            continue;

          lightPosition = light->get_light_position();
          if (qAbs(mouseX - lightPosition.x()) < lightWidth &&
              qAbs(mouseY - lightPosition.y()) < lightHeight &&
              m_light)
          {
            remove_light(light);
            removed = true;
            break;
          }
        }
        if (removed)
          break;
      }
      if (!removed)
        stopAddingLight();
    }
  }
  need_to_update = true;
}

void OpenGlWidget::mouseMoveEvent(QMouseEvent *event)
{
  float mouseX = (float)event->localPos().x() / width() * 2 - 1;
  float mouseY = -(float)event->localPos().y() / height() * 2 + 1;
  QVector3D newLightPos(mouseX, mouseY, currentLight->get_height());

  if (addLight)
  {
    update_light_position(newLightPos);
    need_to_update = true;
    return;
  }

  if (event->buttons() & (Qt::LeftButton | Qt::MidButton))
  {
    if (currentBrush && currentBrush->get_selected() && !lightSelected &&
        event->buttons() & Qt::LeftButton)
    {
      QPoint tpos;
      if (!processor->get_tile_x())
        tpos.setX((event->localPos().x() -
                   ((processor->get_position()->x() + 1) * width() -
                    processor->get_texture()->size().width() *
                        processor->get_zoom()) *
                       0.5) /
                  processor->get_zoom());
      else
        tpos.setX((event->localPos().x()) / processor->get_zoom());

      if (!processor->get_tile_y())
        tpos.setY((event->localPos().y() -
                   ((-processor->get_position()->y() + 1) * height() -
                    processor->get_texture()->size().height() *
                        processor->get_zoom()) *
                       0.5) /
                  processor->get_zoom());
      else
        tpos.setY((event->localPos().y()) / processor->get_zoom());

      currentBrush->mouseMove(oldPos, tpos);
      oldPos = tpos;
    }
    else
    {
      foreach (ImageProcessor *processor, processorList)
      {
        if (lightSelected)
          update_light_position(newLightPos);
        else
        {
          if (processor->get_selected())
          {
            if (!processor->get_tile_x())
              processor->get_position()->setX(
                  (mouseX - processor->get_offset()->x()));

            if (!processor->get_tile_y())
              processor->get_position()->setY(
                  (mouseY - processor->get_offset()->y()));
          }
        }
      }
    }
    need_to_update = true;
  }
  else if (event->buttons() & Qt::RightButton)
  {
  }

  if ((currentBrush && currentBrush->get_selected()) ||
      cursor() != QCursor(Qt::ArrowCursor))
    need_to_update = true;
}

void OpenGlWidget::update_light_position(QVector3D new_pos)
{
  float lightWidth = (float)laigter.width() / width() *
                     0.3; // en paintgl la imagen la escalamos por 0.3
  float lightHeight = (float)laigter.height() / height() * 0.3;
  lightPosition.setX(new_pos.x());

  if (lightPosition.x() >= 1 - lightWidth / 2)
    lightPosition.setX(1 - lightWidth / 2);
  else if (lightPosition.x() < -1 + lightWidth / 2)
    lightPosition.setX(-1 + lightWidth / 2);

  lightPosition.setY(new_pos.y());

  if (lightPosition.y() > 1 - lightHeight / 2)
    lightPosition.setY(1 - lightHeight / 2);
  else if (lightPosition.y() < -1 + lightHeight / 2)
    lightPosition.setY(-1 + lightHeight / 2);

  currentLight->set_light_position(lightPosition);
}

void OpenGlWidget::mouseReleaseEvent(QMouseEvent *event)
{
  lightSelected = false;
}

void OpenGlWidget::setLight(bool light)
{
  m_light = light;
  need_to_update = true;
}

void OpenGlWidget::setParallaxHeight(int height)
{
  parallax_height = height / 1000.0;
  need_to_update = true;
}

void OpenGlWidget::setLightColor(QColor color)
{
  currentLight->set_diffuse_color(color);
  need_to_update = true;
}

void OpenGlWidget::setSpecColor(QColor color)
{
  currentLight->set_specular_color(color);
  need_to_update = true;
}

void OpenGlWidget::setBackgroundColor(QColor color)
{
  backgroundColor = color;
  need_to_update = true;
}

void OpenGlWidget::setLightHeight(float height)
{
  lightPosition = currentLight->get_light_position();
  lightPosition.setZ(height);
  currentLight->set_light_position(lightPosition);
  need_to_update = true;
}

void OpenGlWidget::setLightIntensity(float intensity)
{
  currentLight->set_diffuse_intensity(intensity);
  need_to_update = true;
}

void OpenGlWidget::setSpecIntensity(float intensity)
{
  currentLight->set_specular_intensity(intensity);
  need_to_update = true;
}

void OpenGlWidget::setSpecScatter(int scatter)
{
  currentLight->set_specular_scatter(scatter);
  need_to_update = true;
}

void OpenGlWidget::setAmbientColor(QColor color)
{
  ambientColor = color;
  need_to_update = true;
}

void OpenGlWidget::setAmbientIntensity(float intensity)
{
  ambientIntensity = intensity;
  need_to_update = true;
}

void OpenGlWidget::setPixelated(bool pixelated)
{
  m_pixelated = pixelated;
  need_to_update = true;
}

void OpenGlWidget::setToon(bool toon)
{
  m_toon = toon;
  need_to_update = true;
}

void OpenGlWidget::setPixelSize(int size) { pixelSize = size; }

QImage OpenGlWidget::renderBuffer() { return grabFramebuffer(); }

QImage OpenGlWidget::calculate_preview(bool fullPreview)
{
  QImage renderedPreview;
  if (!fullPreview)
  {
    QString aux;
    QImage n;
    QString suffix;
    QFileInfo info;
    foreach (ImageProcessor *processor, processorList)
    {
      setImage(&processor->texture);
      if (processor->get_current_frame()->get_image(TextureTypes::Normal,
                                                    &normalMap))
        setNormalMap(&normalMap);

      setSpecularMap(processor->get_specular());
      setParallaxMap(processor->get_parallax());
      setOcclusionMap(processor->get_occlusion());
      QOpenGLFramebufferObject frameBuffer(m_image.width(),
                                           m_image.height());
      QMatrix4x4 transform;
      transform.setToIdentity();

      /* Start first pass */
      frameBuffer.bind();

      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      glViewport(0, 0, m_image.width(), m_image.height());
      m_program.bind();

      VAO.bind();

      int i1 = m_pixelated ? GL_NEAREST_MIPMAP_NEAREST
                           : GL_LINEAR_MIPMAP_LINEAR;
      int i2 = m_pixelated ? GL_NEAREST : GL_LINEAR;

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, i1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, i2);

      if (processor->get_tile_x() || processor->get_tile_y())
      {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      }
      else
      {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_BORDER);
      }

      glActiveTexture(GL_TEXTURE0);
      m_program.setUniformValue("view_mode", Preview);
      m_program.setUniformValue("transform", transform);
      m_program.setUniformValue("pixelsX", pixelsX);
      m_program.setUniformValue("pixelsY", pixelsY);
      m_program.setUniformValue("pixelSize", pixelSize);
      m_program.setUniformValue("pixelated", m_pixelated);
      m_program.setUniformValue("toon", m_toon);
      m_program.setUniformValue("selected", false);
      m_program.setUniformValue("ratio", QVector2D(1, 1));

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, i1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, i2);

      m_texture->bind(0);
      m_program.setUniformValue("texture", 0);

      m_normalTexture->bind(1);
      m_program.setUniformValue("normalMap", 1);

      m_parallaxTexture->bind(2);
      m_program.setUniformValue("parallaxMap", 2);

      m_specularTexture->bind(3);
      m_program.setUniformValue("specularMap", 3);

      m_occlusionTexture->bind(4);
      m_program.setUniformValue("occlusionMap", 4);

      float scaleX = !processor->get_tile_x() ? sx : 1;
      float scaleY = !processor->get_tile_y() ? sy : 1;
      float zoomX = !processor->get_tile_x() ? processor->get_zoom() : 1;
      float zoomY = !processor->get_tile_y() ? processor->get_zoom() : 1;

      m_program.setUniformValue(
          "viewPos", QVector3D(-processor->get_position()->x(),
                               -processor->get_position()->y(), 1));
      m_program.setUniformValue("parallax", processor->get_is_parallax());
      m_program.setUniformValue("height_scale", parallax_height);

      QVector3D pos((lightPosition.x() - processor->get_position()->x()) /
                        scaleX / zoomX,
                    (lightPosition.y() - processor->get_position()->y()) /
                        scaleY / zoomY,
                    lightPosition.z());

      apply_light_params();
      m_texture->bind(0);
      glDrawArrays(GL_QUADS, 0, 4);

      m_program.release();
      frameBuffer.release();

      renderedPreview = frameBuffer.toImage();
      if (m_autosave)
      {
        if (exportBasePath == "")
        {
          info = QFileInfo(processor->get_name());
          suffix = info.completeSuffix();
          if (suffix == "")
            suffix = "png";
          aux = info.absoluteFilePath().remove("." + suffix) + "_v." +
                suffix;
        }
        else
        {
          info = QFileInfo(processor->get_name());
          suffix = info.completeSuffix();
          aux =
              exportBasePath + "/" + info.baseName() + "_v." + suffix;
          int i = 1;
          while (QFileInfo::exists(aux))
            aux = exportBasePath + "/" + info.baseName() + "(" +
                  QString::number(++i) + ")" + "_v." + suffix;
        }
        renderedPreview.save(aux);
      }
    }
  }
  else
  {
    QOpenGLFramebufferObject frameBuffer(width(), height());
    frameBuffer.bind();
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    QVector3D color;
    double r, g, b;
    GLfloat bkColor[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, bkColor);

    int i1 =
        m_pixelated ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
    int i2 = m_pixelated ? GL_NEAREST : GL_LINEAR;
    int xmin = width(), xmax = 0, ymin = height(), ymax = 0;

    QMatrix4x4 transform;
    foreach (ImageProcessor *processor, processorList)
    {
      /* Calculate positions for cropping after rendering */
      int xi =
          0.5 * (processor->get_position()->x() + 1) * width() -
          processor->get_texture()->width() / 2.0 * processor->get_zoom();
      int xf =
          0.5 * (processor->get_position()->x() + 1) * width() +
          processor->get_texture()->width() / 2.0 * processor->get_zoom();
      int yi = 0.5 * (-processor->get_position()->y() + 1) * height() -
               processor->get_texture()->height() / 2.0 *
                   processor->get_zoom();
      int yf = 0.5 * (-processor->get_position()->y() + 1) * height() +
               processor->get_texture()->height() / 2.0 *
                   processor->get_zoom();
      if (processor->get_tile_x())
      {
        xmin = 0;
        xmax = width() - 1;
      }
      else
      {
        if (xi < xmin)
          xmin = xi;
        if (xf > xmax)
          xmax = xf;
      }

      if (processor->get_tile_y())
      {
        ymin = 0;
        ymax = height() - 1;
      }
      else
      {
        if (yi < ymin)
          ymin = yi;
        if (yf > ymax)
          ymax = yf;
      }

      setImage(&processor->texture);
      if (processor->get_current_frame()->get_image(TextureTypes::Normal,
                                                    &normalMap))
        setNormalMap(&normalMap);

      setSpecularMap(processor->get_specular());
      setParallaxMap(processor->get_parallax());
      setOcclusionMap(processor->get_occlusion());
      transform.setToIdentity();

      QVector3D texPos = *processor->get_position();
      if (processor->get_tile_x())
        texPos.setX(0);
      if (processor->get_tile_y())
        texPos.setY(0);

      transform.translate(texPos);
      float scaleX = !processor->get_tile_x() ? sx : 1;
      float scaleY = !processor->get_tile_y() ? sy : 1;
      transform.scale(scaleX, scaleY, 1);
      float zoomX = !processor->get_tile_x() ? processor->get_zoom() : 1;
      float zoomY = !processor->get_tile_y() ? processor->get_zoom() : 1;
      transform.scale(zoomX, zoomY, 1);

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, i1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, i2);

      /* Start first pass */
      m_program.bind();
      VAO.bind();

      if (processor->get_tile_x() || processor->get_tile_y())
      {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      }
      else
      {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_BORDER);
      }

      glActiveTexture(GL_TEXTURE0);
      m_program.setUniformValue("view_mode", Preview);
      m_program.setUniformValue("transform", transform);
      m_program.setUniformValue("pixelsX", pixelsX);
      m_program.setUniformValue("pixelsY", pixelsY);
      m_program.setUniformValue("pixelSize", pixelSize);
      m_program.setUniformValue("pixelated", m_pixelated);
      m_program.setUniformValue("toon", m_toon);
      backgroundColor.getRgbF(&r, &g, &b, nullptr);
      color = QVector3D(bkColor[0], bkColor[1], bkColor[2]);
      m_program.setUniformValue("outlineColor", color);
      m_program.setUniformValue("selected", false);
      scaleX = processor->get_tile_x() ? sx : 1;
      scaleY = processor->get_tile_y() ? sy : 1;
      zoomX = processor->get_tile_x() ? processor->get_zoom() : 1;
      zoomY = processor->get_tile_y() ? processor->get_zoom() : 1;
      m_program.setUniformValue(
          "ratio", QVector2D(1 / scaleX / zoomX, 1 / scaleY / zoomY));

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, i1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, i2);

      m_texture->bind(0);
      m_program.setUniformValue("diffuse", 0);

      m_normalTexture->bind(1);
      m_program.setUniformValue("normalMap", 1);

      m_parallaxTexture->bind(2);
      m_program.setUniformValue("parallaxMap", 2);

      m_specularTexture->bind(3);
      m_program.setUniformValue("specularMap", 3);

      m_occlusionTexture->bind(4);
      m_program.setUniformValue("occlusionMap", 4);

      m_program.setUniformValue("viewPos", QVector3D(0, 0, 1));
      m_program.setUniformValue("parallax",
                                processor->get_is_parallax() &&
                                    viewmode == Preview);
      m_program.setUniformValue("height_scale", parallax_height);

      apply_light_params();
      // m_texture->bind(0);
      glDrawArrays(GL_QUADS, 0, 4);

      m_program.release();
    }

    renderedPreview = frameBuffer.toImage();
    QRect rect(QPoint(xmin, ymin), QPoint(xmax, ymax));
    renderedPreview = renderedPreview.copy(rect);
  }
  return renderedPreview;
}

QImage OpenGlWidget::get_preview(bool fullPreview, bool autosave,
                                 QString basePath)
{
  m_fullPreview = fullPreview;
  m_autosave = autosave;
  exportBasePath = basePath;
  export_render = true;
  need_to_update = true;
  while (export_render)
    QApplication::processEvents();

  return renderedPreview;
}

void OpenGlWidget::apply_light_params()
{
  double r, g, b;
  QVector3D color;

  QList<LightSource *> currentLightList;
  if (sample_light_list_used)
    currentLightList = *sampleLightList;
  else
  {
    foreach (ImageProcessor *processor, processorList)
    {
      foreach (LightSource *l, *processor->get_light_list_ptr())
        currentLightList.append(l);
    }
  }

  int n = currentLightList.count();
  if (n == 0)
    return;

  m_program.setUniformValue("lightNum", n);
  for (int i = 0; i < n; i++)
  {
    LightSource *light = currentLightList.at(i);
    light->get_diffuse_color().getRgbF(&r, &g, &b, nullptr);
    color = QVector3D(r, g, b);
    QString Light = "Light[" + QString::number(i) + "]";
    m_program.setUniformValue((Light + ".lightPos").toUtf8().constData(),
                              light->get_light_position());
    m_program.setUniformValue((Light + ".lightColor").toUtf8().constData(),
                              color);
    light->get_specular_color().getRgbF(&r, &g, &b, nullptr);
    color = QVector3D(r, g, b);
    m_program.setUniformValue((Light + ".specColor").toUtf8().constData(),
                              color);
    m_program.setUniformValue(
        (Light + ".diffIntensity").toUtf8().constData(),
        light->get_diffuse_intensity());
    m_program.setUniformValue(
        (Light + ".specIntensity").toUtf8().constData(),
        light->get_specular_intesity());
    m_program.setUniformValue((Light + ".specScatter").toUtf8().constData(),
                              light->get_specular_scatter());
    ambientColor.getRgbF(&r, &g, &b, nullptr);
    color = QVector3D(r, g, b);
    m_program.setUniformValue("ambientColor", color);
    m_program.setUniformValue("ambientIntensity", ambientIntensity);
  }
}

void OpenGlWidget::set_add_light(bool add)
{
  if (add)
  {
    LightSource *l = new LightSource();
    l->copy_settings(currentLight);
    select_light(l);
    if (sample_light_list_used)
      sampleLightList->append(l);
    else
      currentLightList->append(l);

    need_to_update = true;
  }
  else if (addLight)
    remove_light(currentLight);

  addLight = add;
}

void OpenGlWidget::remove_light(LightSource *light)
{
  QList<LightSource *> *lList;
  if (sample_light_list_used)
    lList = sampleLightList;
  else
    lList = currentLightList;

  if (lList->count() > 1)
  {
    lList->removeOne(light);
    if (currentLight == light)
      select_light(lList->last());

    delete light;
    need_to_update = true;
  }
}

void OpenGlWidget::select_light(LightSource *light)
{
  currentLight = light;
  selectedLightChanged(currentLight);
}

QList<LightSource *> *OpenGlWidget::get_current_light_list_ptr()
{
  return currentLightList;
}

void OpenGlWidget::set_processor_list(QList<ImageProcessor *> list)
{
  processorList = list;
}

QList<ImageProcessor *> *OpenGlWidget::get_processor_list(){
  return &processorList;
}

void OpenGlWidget::clear_processor_list()
{
  set_all_processors_selected(false);
  processorList.clear();
}

void OpenGlWidget::add_processor(ImageProcessor *p)
{
  processorList.append(p);
  set_current_processor(p);
}

void OpenGlWidget::set_current_processor(ImageProcessor *p) { processor = p; }

ImageProcessor *OpenGlWidget::get_current_processor() { return processor; }

void OpenGlWidget::set_processor_selected(ImageProcessor *processor,
                                          bool selected)
{
  processor_selected(processor, selected);
}

void OpenGlWidget::set_all_processors_selected(bool selected)
{
  foreach (ImageProcessor *processor, processorList)
    set_processor_selected(processor, selected);
}

QList<ImageProcessor *> OpenGlWidget::get_all_selected_processors()
{
  QList<ImageProcessor *> list;
  foreach (ImageProcessor *p, processorList)
  {
    if (p->get_selected())
      list.append(p);
  }
  return list;
}

void OpenGlWidget::set_view_mode(int mode) { viewmode = mode; }

void OpenGlWidget::use_sample_light_list(bool l)
{
  sample_light_list_used = l;
  need_to_update = true;
}

void OpenGlWidget::set_current_light_list(QList<LightSource *> *list)
{
  currentLightList = list;
  select_light(currentLightList->last());
  need_to_update = true;
}