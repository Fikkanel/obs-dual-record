#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>

#include <obs.h>

#include "DualRecorder.h"

// Small widget that renders a live preview of a single OBS source using
// obs_display.
class SourcePreview : public QWidget {
	Q_OBJECT
public:
	explicit SourcePreview(QWidget *parent = nullptr);
	~SourcePreview() override;

	void SetSource(obs_source_t *src);
	obs_source_t *GetSource() const { return source_; }

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private:
	void CreateDisplay();
	void DestroyDisplay();
	static void DrawPreview(void *data, uint32_t cx, uint32_t cy);

	obs_source_t *source_ = nullptr;
	obs_display_t *display_ = nullptr;
};

// Simple horizontal audio level meter widget (green/yellow/red bar).
class AudioLevelMeter : public QWidget {
	Q_OBJECT
public:
	explicit AudioLevelMeter(QWidget *parent = nullptr);

	// level in range [0.0, 1.0]
	void SetLevel(float level);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	float level_ = 0.0f;
};

// Panel dock
class DualRecordDock : public QWidget {
	Q_OBJECT

public:
	explicit DualRecordDock(QWidget *parent = nullptr);
	~DualRecordDock() override;

	void OnOBSFinishedLoading();

private slots:
	void OnRefreshClicked();
	void OnBrowseClicked();
	void OnStartStopClicked();
	void OnTimerTick();
	void OnComboAChanged(int index);
	void OnComboBChanged(int index);

private:
	void PopulateWindowCombo(QComboBox *combo);
	SlotConfig BuildSlotConfig(QComboBox *combo, QLineEdit *labelEdit);
	void SetRecordingUiState(bool recording);
	void ShowError(const QString &msg);
	void UpdatePreview(QComboBox *combo, SourcePreview *preview, obs_source_t *&previewSrc,
			   obs_source_t *&audioMonSrc, obs_volmeter_t *&volmeter,
			   AudioLevelMeter *meter, int slotIndex);
	void CleanupAudioMonitor(obs_source_t *&audioMonSrc, obs_volmeter_t *&volmeter, int slotIndex);

	QComboBox *comboA_ = nullptr;
	QComboBox *comboB_ = nullptr;
	QLineEdit *labelEditA_ = nullptr;
	QLineEdit *labelEditB_ = nullptr;
	QLineEdit *outputFolderEdit_ = nullptr;
	QPushButton *refreshBtn_ = nullptr;
	QPushButton *browseBtn_ = nullptr;
	QPushButton *startStopBtn_ = nullptr;
	QLabel *statusLabel_ = nullptr;

	// Video previews
	SourcePreview *previewA_ = nullptr;
	SourcePreview *previewB_ = nullptr;
	obs_source_t *previewSrcA_ = nullptr;
	obs_source_t *previewSrcB_ = nullptr;

	// Audio monitoring (volmeter)
	AudioLevelMeter *meterA_ = nullptr;
	AudioLevelMeter *meterB_ = nullptr;
	obs_source_t *audioMonSrcA_ = nullptr;
	obs_source_t *audioMonSrcB_ = nullptr;
	obs_volmeter_t *volmeterA_ = nullptr;
	obs_volmeter_t *volmeterB_ = nullptr;

	QTimer uiTimer_;
	QElapsedTimer elapsed_;

	DualRecorder recorder_;

	bool obsReady_ = false;
};
