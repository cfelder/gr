#include <QPainter>
#include <QResizeEvent>
#include <QtGlobal>
#include <cmath>
#include <iostream>
#include <sstream>
#include "util.hxx"
#include "gr_widget.hxx"

GRWidget::GRWidget(QWidget *parent) : QWidget(parent), args_(nullptr), box_zoom_rubberband_(nullptr)
{
  init_env_vars();
  init_plot_data();
  init_ui();
}

GRWidget::~GRWidget()
{
  grm_args_delete(args_);
}

void GRWidget::init_env_vars()
{
  setenv("GKS_WSTYPE", "381", 1);
  setenv("GKS_DOUBLE_BUF", "True", 1);
}

void GRWidget::init_plot_data()
{
  const int n = 1000;
  double subplot_data[4][2][1000];
  grm_args_t *subplots[4];

  for (int i = 0; i < 2; ++i)
    {
      for (int j = 0; j < n; ++j)
        {
          subplot_data[i][0][j] = j * 2 * M_PI / n;
          subplot_data[i][1][j] = sin((j * (i + 1) * 2) * M_PI / n);
        }
    }
  for (int i = 0; i < 2; ++i)
    {
      for (int j = 0; j < n; ++j)
        {
          subplot_data[2 + i][0][j] = j * 2 * M_PI / n;
          subplot_data[2 + i][1][j] = cos((j * (i + 1) * 2) * M_PI / n);
        }
    }

  for (int i = 0; i < 4; ++i)
    {
      subplots[i] = grm_args_new();
      grm_args_push(subplots[i], "x", "nD", n, subplot_data[i][0]);
      grm_args_push(subplots[i], "y", "nD", n, subplot_data[i][1]);
      grm_args_push(subplots[i], "subplot", "dddd", 0.5 * (i % 2), 0.5 * (i % 2 + 1), 0.5 * (i / 2), 0.5 * (i / 2 + 1));
      grm_args_push(subplots[i], "keep_aspect_ratio", "i", 1);
      // grm_args_push(subplots[i], "backgroundcolor", "i", i + 1);
    }

  args_ = grm_args_new();
  grm_args_push(args_, "subplots", "nA", 4, subplots);
  grm_merge(args_);
}

void GRWidget::init_ui()
{
  box_zoom_rubberband_ = new QRubberBand(QRubberBand::Rectangle, this);
  setFocusPolicy(Qt::FocusPolicy::StrongFocus); // needed to receive key press events
  setMouseTracking(true);
}

void GRWidget::draw()
{
  grm_plot(nullptr);
}

void GRWidget::paintEvent(QPaintEvent *event)
{
  util::unused(event);
  QPainter painter;
  std::stringstream addresses;

  addresses << static_cast<void *>(this) << "!" << static_cast<void *>(&painter);
  setenv("GKS_CONID", addresses.str().c_str(), 1);

  painter.begin(this);

  painter.fillRect(0, 0, width(), height(), QColor("white"));
  draw();

  painter.end();
}

void GRWidget::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_R)
    {
      grm_args_t *input_args = grm_args_new();
      QPoint widget_cursor_pos = mapFromGlobal(QCursor::pos());
      grm_args_push(input_args, "key", "s", "r");
      grm_args_push(input_args, "x", "i", widget_cursor_pos.x());
      grm_args_push(input_args, "y", "i", widget_cursor_pos.y());
      grm_input(input_args);
      grm_args_delete(input_args);
      repaint();
    }
}

void GRWidget::mouseMoveEvent(QMouseEvent *event)
{
  emit mouse_pos_changed(event->pos());
  if (mouse_state_.mode() == MouseState::Mode::boxzoom)
    {
      box_zoom_rubberband_->setGeometry(QRect(mouse_state_.mode_start_pos(), event->pos()).normalized());
    }
}

void GRWidget::mousePressEvent(QMouseEvent *event)
{
  mouse_state_.mode_start_pos(event->pos());
  if (event->button() == Qt::MouseButton::LeftButton)
    {
      mouse_state_.mode(MouseState::Mode::boxzoom);
      box_zoom_rubberband_->setGeometry(QRect(mouse_state_.mode_start_pos(), QSize()));
      box_zoom_rubberband_->show();
    }
  else if (event->button() == Qt::MouseButton::RightButton)
    {
      mouse_state_.mode(MouseState::Mode::pan);
    }
}

void GRWidget::mouseReleaseEvent(QMouseEvent *event)
{
  QPoint start_pos{mouse_state_.mode_start_pos()};
  grm_args_t *input_args = grm_args_new();

  std::cout << "mouse delta: " << event->pos() - start_pos << std::endl;

  if (mouse_state_.mode() == MouseState::Mode::boxzoom)
    {
      box_zoom_rubberband_->hide();
      if (std::abs(event->x() - start_pos.x()) >= 5 && std::abs(event->y() - start_pos.y()) >= 5)
        {
          grm_args_push(input_args, "keep_aspect_ratio", "i", event->modifiers() & Qt::ShiftModifier);
          grm_args_push(input_args, "x1", "i", start_pos.x());
          grm_args_push(input_args, "y1", "i", start_pos.y());
          grm_args_push(input_args, "x2", "i", event->x());
          grm_args_push(input_args, "y2", "i", event->y());
        }
    }
  else if (mouse_state_.mode() == MouseState::Mode::pan)
    {
      grm_args_push(input_args, "x", "i", start_pos.x());
      grm_args_push(input_args, "y", "i", start_pos.y());
      grm_args_push(input_args, "xshift", "i", event->x() - start_pos.x());
      grm_args_push(input_args, "yshift", "i", event->y() - start_pos.y());
    }

  grm_input(input_args);
  grm_args_delete(input_args);

  repaint();
}

void GRWidget::resizeEvent(QResizeEvent *event)
{
  grm_args_push(args_, "size", "dd", (double)event->size().width(), (double)event->size().height());
  grm_merge(args_);
}

void GRWidget::wheelEvent(QWheelEvent *event)
{
  int x, y;

#if QT_VERSION <= QT_VERSION_CHECK(5, 14, 2)
  x = event->pos().x();
  y = event->pos().y();
#else
  x = (int)event->position().x();
  y = (int)event->position().y();
#endif

  std::cout << "angle delta: " << event->angleDelta() << ", position: " << QPoint(x, y) << std::endl;

  grm_args_t *input_args = grm_args_new();
  grm_args_push(input_args, "x", "i", x);
  grm_args_push(input_args, "y", "i", y);
  grm_args_push(input_args, "angle_delta", "d", (double)event->angleDelta().y());
  grm_input(input_args);
  grm_args_delete(input_args);

  repaint();
}

GRWidget::MouseState::MouseState() : mode_{Mode::normal}, mode_start_pos_{0, 0} {}

GRWidget::MouseState::~MouseState() {}

GRWidget::MouseState::Mode GRWidget::MouseState::mode() const
{
  return mode_;
}

void GRWidget::MouseState::mode(const Mode &mode)
{
  mode_ = mode;
}

QPoint GRWidget::MouseState::mode_start_pos() const
{
  return mode_start_pos_;
}

void GRWidget::MouseState::mode_start_pos(const QPoint &point)
{
  mode_start_pos_ = point;
}
