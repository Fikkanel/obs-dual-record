#include "DualRecordDock.h"

#include <obs.h>
#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <util/windows/window-helpers.h>
#include <plugin-support.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QPainter>
#include <QResizeEvent>
#include <QLinearGradient>

#include <cmath>

// ──────────────────────────────────────────────────────
//  AudioLevelMeter  –  horizontal VU-style bar
// ──────────────────────────────────────────────────────

AudioLevelMeter::AudioLevelMeter(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(8);
	setMaximumHeight(12);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void AudioLevelMeter::SetLevel(float level)
{
	level_ = qBound(0.0f, level, 1.0f);
	update();
}

void AudioLevelMeter::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	int w = width();
	int h = height();

	// Background (dark)
	p.fillRect(0, 0, w, h, QColor(30, 30, 30));

	if (level_ > 0.001f) {
		int barW = static_cast<int>(level_ * w);

		// Gradient: green -> yellow -> red
		QLinearGradient grad(0, 0, w, 0);
		grad.setColorAt(0.0, QColor(0, 180, 60));
		grad.setColorAt(0.6, QColor(0, 200, 60));
		grad.setColorAt(0.75, QColor(220, 200, 0));
		grad.setColorAt(1.0, QColor(220, 40, 40));

		p.fillRect(0, 0, barW, h, grad);
	}

	// Border
	p.setPen(QColor(60, 60, 60));
	p.drawRect(0, 0, w - 1, h - 1);
}

// ──────────────────────────────────────────────────────
//  SourcePreview  –  GPU-backed live preview of a source
// ──────────────────────────────────────────────────────

SourcePreview::SourcePreview(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);

	setMinimumSize(160, 90);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

SourcePreview::~SourcePreview()
{
	DestroyDisplay();
	if (source_) {
		obs_source_dec_showing(source_);
		obs_source_release(source_);
		source_ = nullptr;
	}
}

void SourcePreview::SetSource(obs_source_t *src)
{
	if (source_) {
		DestroyDisplay();
		obs_source_dec_showing(source_);
		obs_source_release(source_);
		source_ = nullptr;
	}
	if (src) {
		source_ = obs_source_get_ref(src);
		if (source_)
			obs_source_inc_showing(source_);
	}
	if (source_ && isVisible())
		CreateDisplay();
	update();
}

void SourcePreview::paintEvent(QPaintEvent *)
{
	if (!display_ && source_)
		CreateDisplay();
}

void SourcePreview::resizeEvent(QResizeEvent *)
{
	if (display_) {
		QSize s = size();
		qreal ratio = devicePixelRatioF();
		obs_display_resize(display_, (uint32_t)(s.width() * ratio),
				   (uint32_t)(s.height() * ratio));
	}
}

void SourcePreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (source_ && !display_)
		CreateDisplay();
}

void SourcePreview::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	DestroyDisplay();
}

void SourcePreview::CreateDisplay()
{
	if (display_)
		return;

	QSize s = size();
	if (s.width() < 2 || s.height() < 2)
		return;

	gs_init_data init_data = {};
	qreal ratio = devicePixelRatioF();
	init_data.cx = (uint32_t)(s.width() * ratio);
	init_data.cy = (uint32_t)(s.height() * ratio);
	init_data.format = GS_BGRA;
	init_data.zsformat = GS_ZS_NONE;

#ifdef _WIN32
	init_data.window.hwnd = (HWND)winId();
#endif

	display_ = obs_display_create(&init_data, 0x1e1e1e);
	if (display_)
		obs_display_add_draw_callback(display_, DrawPreview, this);
}

void SourcePreview::DestroyDisplay()
{
	if (display_) {
		obs_display_remove_draw_callback(display_, DrawPreview, this);
		obs_display_destroy(display_);
		display_ = nullptr;
	}
}

void SourcePreview::DrawPreview(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<SourcePreview *>(data);
	if (!self->source_)
		return;

	uint32_t srcW = obs_source_get_width(self->source_);
	uint32_t srcH = obs_source_get_height(self->source_);
	if (srcW == 0 || srcH == 0)
		return;

	float scaleX = (float)cx / (float)srcW;
	float scaleY = (float)cy / (float)srcH;
	float scale = (scaleX < scaleY) ? scaleX : scaleY;

	int newW = (int)(srcW * scale);
	int newH = (int)(srcH * scale);
	int offsetX = ((int)cx - newW) / 2;
	int offsetY = ((int)cy - newH) / 2;

	gs_viewport_push();
	gs_projection_push();

	gs_ortho(0.0f, (float)srcW, 0.0f, (float)srcH, -100.0f, 100.0f);
	gs_set_viewport(offsetX, offsetY, newW, newH);

	obs_source_video_render(self->source_);

	gs_projection_pop();
	gs_viewport_pop();
}

// ──────────────────────────────────────────────────────
//  Volmeter callback — called from audio thread
// ──────────────────────────────────────────────────────

struct VolmeterCallbackData {
	AudioLevelMeter *meter;
};

static void volmeter_callback(void *data, const float magnitude[MAX_AUDIO_CHANNELS],
			       const float peak[MAX_AUDIO_CHANNELS],
			       const float inputPeak[MAX_AUDIO_CHANNELS])
{
	UNUSED_PARAMETER(magnitude);
	UNUSED_PARAMETER(inputPeak);

	auto *cbd = static_cast<VolmeterCallbackData *>(data);
	if (!cbd || !cbd->meter)
		return;

	// Take the max peak across channels (typically stereo = 2 channels)
	float maxPeak = -INFINITY;
	for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		if (peak[i] > maxPeak)
			maxPeak = peak[i];
	}

	// peak is in dB. Convert to 0..1 range.
	// -60 dB = silence, 0 dB = max
	float level = 0.0f;
	if (maxPeak > -60.0f) {
		level = (maxPeak + 60.0f) / 60.0f;
		if (level > 1.0f) level = 1.0f;
		if (level < 0.0f) level = 0.0f;
	}

	// Must update UI from main thread
	QMetaObject::invokeMethod(cbd->meter, [cbd, level]() {
		cbd->meter->SetLevel(level);
	}, Qt::QueuedConnection);
}

// Global storage for callback data (simple approach, max 2 slots)
static VolmeterCallbackData g_volCbData[2];

// ──────────────────────────────────────────────────────
//  Helper
// ──────────────────────────────────────────────────────

namespace {
QGroupBox *MakeSlotGroup(const QString &title, QComboBox *combo, QLineEdit *labelEdit,
			 const QString &defaultLabel, SourcePreview *preview,
			 AudioLevelMeter *meter)
{
	auto *group = new QGroupBox(title);
	group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	auto *layout = new QVBoxLayout();
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(4);

	layout->addWidget(new QLabel(QObject::tr("Aplikasi / jendela:")));
	combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	combo->setMinimumWidth(100);
	combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	combo->setMinimumContentsLength(12);
	layout->addWidget(combo);

	// Preview (shows live feed of the selected window)
	preview->setMinimumHeight(100);
	preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	layout->addWidget(preview, 1);

	// Audio level meter
	layout->addWidget(meter);

	auto *nameLayout = new QHBoxLayout();
	nameLayout->setSpacing(4);
	nameLayout->addWidget(new QLabel(QObject::tr("Nama file:")));
	labelEdit->setText(defaultLabel);
	labelEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	nameLayout->addWidget(labelEdit, 1);
	layout->addLayout(nameLayout);

	group->setLayout(layout);
	return group;
}

QString SanitizeFileName(const QString &input)
{
	QString out = input;
	static const QString invalid = "\\/:*?\"<>|";
	for (const QChar &c : invalid)
		out.replace(c, "_");
	return out.trimmed().isEmpty() ? "rekaman" : out.trimmed();
}
} // namespace

// ──────────────────────────────────────────────────────
//  DualRecordDock  –  main UI
// ──────────────────────────────────────────────────────

DualRecordDock::DualRecordDock(QWidget *parent) : QWidget(parent)
{
	auto *mainLayout = new QVBoxLayout();
	mainLayout->setContentsMargins(8, 8, 8, 8);
	mainLayout->setSpacing(6);

	auto *intro = new QLabel(tr("Rekam 2 aplikasi meeting sekaligus (mis. Zoom + Chrome/Meet) ke 2 file "
				    "MP4 terpisah. Audio masing-masing TIDAK akan tercampur."));
	intro->setWordWrap(true);
	mainLayout->addWidget(intro);

	comboA_ = new QComboBox();
	labelEditA_ = new QLineEdit();
	comboB_ = new QComboBox();
	labelEditB_ = new QLineEdit();

	previewA_ = new SourcePreview();
	previewB_ = new SourcePreview();
	meterA_ = new AudioLevelMeter();
	meterB_ = new AudioLevelMeter();

	// Slot A dan Slot B side-by-side (layout LEBAR) dengan ukuran 50:50
	auto *slotsLayout = new QHBoxLayout();
	slotsLayout->setSpacing(6);
	slotsLayout->addWidget(MakeSlotGroup(tr("Slot A"), comboA_, labelEditA_, "Kelas_Zoom",
					     previewA_, meterA_), 1);
	slotsLayout->addWidget(MakeSlotGroup(tr("Slot B"), comboB_, labelEditB_, "Kelas_Meet",
					     previewB_, meterB_), 1);
	mainLayout->addLayout(slotsLayout, 1);

	refreshBtn_ = new QPushButton(tr("Refresh daftar jendela"));
	mainLayout->addWidget(refreshBtn_);

	auto *folderLayout = new QHBoxLayout();
	outputFolderEdit_ = new QLineEdit();
	outputFolderEdit_->setText(
		QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
	browseBtn_ = new QPushButton(tr("Pilih folder..."));
	folderLayout->addWidget(new QLabel(tr("Simpan ke:")));
	folderLayout->addWidget(outputFolderEdit_, 1);
	folderLayout->addWidget(browseBtn_);
	mainLayout->addLayout(folderLayout);

	startStopBtn_ = new QPushButton(tr("Start Dual Record"));
	startStopBtn_->setStyleSheet("font-weight: bold; padding: 6px;");
	mainLayout->addWidget(startStopBtn_);

	statusLabel_ = new QLabel(tr("Belum merekam."));
	mainLayout->addWidget(statusLabel_);

	mainLayout->addStretch(0);
	setLayout(mainLayout);

	connect(refreshBtn_, &QPushButton::clicked, this, &DualRecordDock::OnRefreshClicked);
	connect(browseBtn_, &QPushButton::clicked, this, &DualRecordDock::OnBrowseClicked);
	connect(startStopBtn_, &QPushButton::clicked, this, &DualRecordDock::OnStartStopClicked);
	connect(&uiTimer_, &QTimer::timeout, this, &DualRecordDock::OnTimerTick);
	uiTimer_.setInterval(1000);

	connect(comboA_, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &DualRecordDock::OnComboAChanged);
	connect(comboB_, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &DualRecordDock::OnComboBChanged);

	recorder_.OnUnexpectedStop = [this](const std::string &reason) {
		QMetaObject::invokeMethod(this, [this, reason]() {
			SetRecordingUiState(false);
			ShowError(QString::fromStdString(reason));
		}, Qt::QueuedConnection);
	};

	PopulateWindowCombo(comboA_);
	PopulateWindowCombo(comboB_);
}

DualRecordDock::~DualRecordDock()
{
	if (recorder_.IsRecording())
		recorder_.Stop();

	CleanupAudioMonitor(audioMonSrcA_, volmeterA_, 0);
	CleanupAudioMonitor(audioMonSrcB_, volmeterB_, 1);

	if (previewSrcA_) {
		previewA_->SetSource(nullptr);
		obs_source_release(previewSrcA_);
		previewSrcA_ = nullptr;
	}
	if (previewSrcB_) {
		previewB_->SetSource(nullptr);
		obs_source_release(previewSrcB_);
		previewSrcB_ = nullptr;
	}
}

void DualRecordDock::OnOBSFinishedLoading()
{
	obsReady_ = true;
	obs_log(LOG_INFO, "[obs-dual-record] OBS finished loading, creating preview sources...");
	UpdatePreview(comboA_, previewA_, previewSrcA_, audioMonSrcA_, volmeterA_, meterA_, 0);
	UpdatePreview(comboB_, previewB_, previewSrcB_, audioMonSrcB_, volmeterB_, meterB_, 1);
}

void DualRecordDock::PopulateWindowCombo(QComboBox *combo)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p =
		obs_properties_add_list(props, "window", "Window", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	ms_fill_window_list(p, INCLUDE_MINIMIZED, nullptr);

	QString previous = combo->currentData().toString();
	combo->clear();

	size_t count = obs_property_list_item_count(p);
	for (size_t i = 0; i < count; i++) {
		const char *name = obs_property_list_item_name(p, i);
		const char *val = obs_property_list_item_string(p, i);
		combo->addItem(QString::fromUtf8(name ? name : ""), QString::fromUtf8(val ? val : ""));
	}

	obs_properties_destroy(props);

	if (!previous.isEmpty()) {
		int idx = combo->findData(previous);
		if (idx >= 0)
			combo->setCurrentIndex(idx);
	}
}

void DualRecordDock::CleanupAudioMonitor(obs_source_t *&audioMonSrc, obs_volmeter_t *&volmeter, int slotIndex)
{
	if (volmeter) {
		obs_volmeter_remove_callback(volmeter, volmeter_callback, &g_volCbData[slotIndex]);
		obs_volmeter_destroy(volmeter);
		volmeter = nullptr;
	}
	if (audioMonSrc) {
		// Clear the output channel
		uint32_t ch = 20 + static_cast<uint32_t>(slotIndex);
		obs_source_t *src = obs_get_output_source(ch);
		if (src) {
			if (src == audioMonSrc)
				obs_set_output_source(ch, nullptr);
			obs_source_release(src);
		}
		obs_source_release(audioMonSrc);
		audioMonSrc = nullptr;
	}
}

void DualRecordDock::UpdatePreview(QComboBox *combo, SourcePreview *preview, obs_source_t *&previewSrc,
				   obs_source_t *&audioMonSrc, obs_volmeter_t *&volmeter,
				   AudioLevelMeter *meter, int slotIndex)
{
	if (!obsReady_)
		return;

	// ── Cleanup old video preview ──
	if (previewSrc) {
		preview->SetSource(nullptr);
		obs_source_release(previewSrc);
		previewSrc = nullptr;
	}

	// ── Cleanup old audio monitor ──
	CleanupAudioMonitor(audioMonSrc, volmeter, slotIndex);
	meter->SetLevel(0.0f);

	QString windowVal = combo->currentData().toString();
	if (windowVal.isEmpty())
		return;

	// ── Create video preview source (window_capture) ──
	{
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "window", windowVal.toUtf8().constData());
		obs_data_set_int(settings, "priority", 2 /* WINDOW_PRIORITY_EXE */);

		QString srcName = QString("dual_rec_preview_%1").arg(slotIndex);
		previewSrc = obs_source_create_private("window_capture",
						       srcName.toUtf8().constData(), settings);
		obs_data_release(settings);

		if (previewSrc) {
			obs_log(LOG_INFO, "[obs-dual-record] Preview source %d created OK", slotIndex);
			preview->SetSource(previewSrc);
		}
	}

	// ── Create audio monitor source (wasapi_process_output_capture) ──
	// This is a private source ONLY for monitoring audio levels in the UI.
	// It is NOT used for recording — DualRecorder creates its own sources.
	{
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "window", windowVal.toUtf8().constData());
		obs_data_set_int(settings, "priority", 2 /* WINDOW_PRIORITY_EXE */);

		QString srcName = QString("dual_rec_audiomon_%1").arg(slotIndex);
		audioMonSrc = obs_source_create_private("wasapi_process_output_capture",
							srcName.toUtf8().constData(), settings);
		obs_data_release(settings);

		if (audioMonSrc) {
			// Set audio_mixers to 0 so this monitoring source doesn't
			// contaminate any mixer track. The volmeter hooks into the
			// source's raw audio output directly, before mixer routing.
			obs_source_set_audio_mixers(audioMonSrc, 0);

			// Place on a high output channel so OBS processes it (makes it active)
			uint32_t ch = 20 + static_cast<uint32_t>(slotIndex);
			obs_set_output_source(ch, audioMonSrc);

			// Create volmeter and attach
			volmeter = obs_volmeter_create(OBS_FADER_LOG);
			obs_volmeter_attach_source(volmeter, audioMonSrc);

			g_volCbData[slotIndex].meter = meter;
			obs_volmeter_add_callback(volmeter, volmeter_callback, &g_volCbData[slotIndex]);

			obs_log(LOG_INFO, "[obs-dual-record] Audio monitor %d created, "
				"volmeter attached, channel=%u", slotIndex, ch);
		}
	}
}

void DualRecordDock::OnComboAChanged(int)
{
	UpdatePreview(comboA_, previewA_, previewSrcA_, audioMonSrcA_, volmeterA_, meterA_, 0);
}

void DualRecordDock::OnComboBChanged(int)
{
	UpdatePreview(comboB_, previewB_, previewSrcB_, audioMonSrcB_, volmeterB_, meterB_, 1);
}

void DualRecordDock::OnRefreshClicked()
{
	PopulateWindowCombo(comboA_);
	PopulateWindowCombo(comboB_);

	UpdatePreview(comboA_, previewA_, previewSrcA_, audioMonSrcA_, volmeterA_, meterA_, 0);
	UpdatePreview(comboB_, previewB_, previewSrcB_, audioMonSrcB_, volmeterB_, meterB_, 1);
}

void DualRecordDock::OnBrowseClicked()
{
	QString dir = QFileDialog::getExistingDirectory(this, tr("Pilih folder simpan rekaman"),
							outputFolderEdit_->text());
	if (!dir.isEmpty())
		outputFolderEdit_->setText(dir);
}

SlotConfig DualRecordDock::BuildSlotConfig(QComboBox *combo, QLineEdit *labelEdit)
{
	SlotConfig cfg;
	cfg.label = combo->currentText().toStdString();
	cfg.windowValue = combo->currentData().toString().toStdString();

	QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
	QString fileName = SanitizeFileName(labelEdit->text()) + "_" + stamp + ".mp4";
	QDir dir(outputFolderEdit_->text());
	cfg.outputPath = dir.filePath(fileName).toStdString();

	return cfg;
}

void DualRecordDock::OnStartStopClicked()
{
	if (recorder_.IsRecording()) {
		recorder_.Stop();
		SetRecordingUiState(false);
		return;
	}

	if (comboA_->count() == 0 || comboB_->count() == 0) {
		ShowError(tr("Daftar jendela kosong. Buka Zoom/Meet lalu klik 'Refresh daftar jendela'."));
		return;
	}

	if (comboA_->currentData().toString() == comboB_->currentData().toString()) {
		ShowError(tr("Slot A dan Slot B tidak boleh menunjuk ke jendela/aplikasi yang sama."));
		return;
	}

	QDir dir(outputFolderEdit_->text());
	if (!dir.exists()) {
		ShowError(tr("Folder tujuan tidak ditemukan. Pilih folder yang valid dulu."));
		return;
	}

	SlotConfig a = BuildSlotConfig(comboA_, labelEditA_);
	SlotConfig b = BuildSlotConfig(comboB_, labelEditB_);

	std::string err;
	if (!recorder_.Start(a, b, err)) {
		ShowError(QString::fromStdString(err));
		return;
	}

	SetRecordingUiState(true);
}

void DualRecordDock::SetRecordingUiState(bool recording)
{
	comboA_->setEnabled(!recording);
	comboB_->setEnabled(!recording);
	labelEditA_->setEnabled(!recording);
	labelEditB_->setEnabled(!recording);
	refreshBtn_->setEnabled(!recording);
	browseBtn_->setEnabled(!recording);
	outputFolderEdit_->setEnabled(!recording);

	startStopBtn_->setText(recording ? tr("Stop Dual Record") : tr("Start Dual Record"));

	if (recording) {
		elapsed_.start();
		uiTimer_.start();
		statusLabel_->setText(tr("Merekam... 00:00:00"));
	} else {
		uiTimer_.stop();
		statusLabel_->setText(tr("Berhenti. File tersimpan di folder yang dipilih."));
	}
}

void DualRecordDock::OnTimerTick()
{
	qint64 ms = elapsed_.elapsed();
	int totalSec = static_cast<int>(ms / 1000);
	int h = totalSec / 3600;
	int m = (totalSec % 3600) / 60;
	int s = totalSec % 60;
	statusLabel_->setText(tr("Merekam... %1").arg(QString::asprintf("%02d:%02d:%02d", h, m, s)));
}

void DualRecordDock::ShowError(const QString &msg)
{
	QMessageBox::warning(this, tr("Dual Record"), msg);
}
