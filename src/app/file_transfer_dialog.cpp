/**
 * @file file_transfer_dialog.cpp
 * @brief 文件传输弹窗实现：
 * - 左侧远程目录树 / 右侧本机目录树
 * - 底部任务表展示上传下载进度、状态、速度
 * - 任务表右键菜单统一承载暂停 / 继续 / 取消操作
 */

#include "file_transfer_dialog.h"
#include "title_bar.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidgetItem>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QFileIconProvider& fileIconProvider() {
  static QFileIconProvider provider;
  return provider;
}

/**
 * 为远程目录项生成“当前系统类型图标”：
 * - 盘符根目录显示磁盘图标
 * - 文件夹显示文件夹图标
 * - 文件按扩展名向系统请求对应类型图标
 *
 * 远程文件并不真实存在于本机文件系统，因此这里只能依据名称/扩展名推导类型；
 * 但在 Windows 下这已经足够得到常见文档、图片、压缩包等关联图标。
 */
QIcon remoteEntryIcon(const rdqt::FileEntry& entry, bool isRoots) {
  auto& provider = fileIconProvider();
  if (isRoots) {
    return provider.icon(QFileIconProvider::Drive);
  }
  if (entry.isDir) {
    return provider.icon(QFileIconProvider::Folder);
  }
  const QFileInfo pseudoInfo(entry.name);
  const QIcon icon = provider.icon(pseudoInfo);
  if (!icon.isNull()) {
    return icon;
  }
  return provider.icon(QFileIconProvider::File);
}

QTreeWidget* createPane(const QString& title, QWidget* parent) {
  auto* tree = new QTreeWidget(parent);
  tree->setObjectName(QStringLiteral("fileTransferTree"));
  tree->setHeaderLabels({title, QStringLiteral("类型"), QStringLiteral("大小")});
  tree->header()->setStyleSheet(
      QStringLiteral(
          "QHeaderView::section{"
          "background:#20232b;"
          "color:#dfe3ea;"
          "font-weight:600;"
          "border:none;"
          "border-right:1px solid #343846;"
          "border-bottom:1px solid #343846;"
          "padding:8px 10px;"
          "}"));
  tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  tree->setAlternatingRowColors(true);
  tree->setRootIsDecorated(false);
  return tree;
}

QString displaySize(qint64 bytes) {
  if (bytes < 0) {
    return QStringLiteral("-");
  }
  static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double size = static_cast<double>(bytes);
  int unitIndex = 0;
  while (size >= 1024.0 && unitIndex < 4) {
    size /= 1024.0;
    ++unitIndex;
  }
  if (unitIndex == 0) {
    return QStringLiteral("%1 %2").arg(static_cast<qint64>(size)).arg(QString::fromLatin1(units[unitIndex]));
  }
  return QStringLiteral("%1 %2").arg(QString::number(size, 'f', 1)).arg(QString::fromLatin1(units[unitIndex]));
}

QWidget* createPaneCard(const QString& title,
                        QLabel** pathLabelOut,
                        QPushButton** backBtnOut,
                        QTreeWidget** treeOut,
                        QWidget* parent) {
  auto* card = new QFrame(parent);
  card->setObjectName(QStringLiteral("fileTransferPane"));
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  auto* titleLabel = new QLabel(title, card);
  titleLabel->setObjectName(QStringLiteral("fileTransferPaneTitle"));
  layout->addWidget(titleLabel);

  auto* pathRow = new QHBoxLayout();
  auto* backBtn = new QPushButton(QStringLiteral("返回上级"), card);
  backBtn->setObjectName(QStringLiteral("fileTransferSubtleBtn"));
  auto* pathLabel = new QLabel(card);
  pathLabel->setObjectName(QStringLiteral("fileTransferPathLabel"));
  pathLabel->setWordWrap(true);
  pathRow->addWidget(backBtn);
  pathRow->addWidget(pathLabel, 1);
  layout->addLayout(pathRow);

  auto* tree = createPane(title, card);
  layout->addWidget(tree, 1);

  *pathLabelOut = pathLabel;
  *backBtnOut = backBtn;
  *treeOut = tree;
  return card;
}

} // namespace

FileTransferDialog::FileTransferDialog(QWidget* parent) : QDialog(parent) {
  setupUi();
  populateRemotePlaceholder();
  populateLocalRoots();
}

void FileTransferDialog::setRemoteStatus(const QString& text) {
  if (m_remotePathLabel) {
    m_remotePathLabel->setText(text);
  }
}

void FileTransferDialog::setRemoteRoots(const QVector<rdqt::FileEntry>& entries) {
  populateRemoteEntries(QString(), entries, true);
}

void FileTransferDialog::setRemoteDirectory(const QString& path, const QVector<rdqt::FileEntry>& entries) {
  populateRemoteEntries(path, entries, false);
}

void FileTransferDialog::markDownloadStarted(const QString& remotePath, const QString& localDir, qint64 totalBytes) {
  const QFileInfo info(remotePath);
  const int row = ensureTaskRow(remotePath, info.fileName(), QStringLiteral("下载"));
  m_taskTable->setItem(row, 2, new QTableWidgetItem(QStringLiteral("0 / %1").arg(displaySize(totalBytes))));
  m_taskTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("下载中 -> %1").arg(QDir::toNativeSeparators(localDir))));
  m_taskTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("-")));
}

void FileTransferDialog::markDownloadProgress(const QString& remotePath,
                                              qint64 transferredBytes,
                                              qint64 totalBytes,
                                              const QString& detail) {
  // 任务表第 3 列固定显示“百分比 + 已传输 / 总大小”，
  // 第 4 列交给主线程写状态，第 5 列单独写速度，方便后续扩展失败/等待恢复等状态。
  const QFileInfo info(remotePath);
  const int row = ensureTaskRow(remotePath, info.fileName(), QStringLiteral("下载"));
  const int percent = totalBytes > 0 ? static_cast<int>((transferredBytes * 100) / totalBytes) : 0;
  m_taskTable->setItem(
      row,
      2,
      new QTableWidgetItem(QStringLiteral("%1% (%2 / %3)").arg(percent).arg(displaySize(transferredBytes)).arg(displaySize(totalBytes))));
  m_taskTable->setItem(row, 3, new QTableWidgetItem(detail));
  m_taskTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("-")));
}

void FileTransferDialog::markDownloadFinished(const QString& remotePath, bool ok, const QString& detail) {
  const QFileInfo info(remotePath);
  const int row = ensureTaskRow(remotePath, info.fileName(), QStringLiteral("下载"));
  m_taskTable->setItem(row, 2, new QTableWidgetItem(ok ? QStringLiteral("100%") : QStringLiteral("失败")));
  m_taskTable->setItem(row, 3, new QTableWidgetItem(detail));
  m_taskTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("-")));
  if (ok && !m_localCurrentPath.isEmpty()) {
    populateLocalDirectory(m_localCurrentPath);
  }
}

void FileTransferDialog::markUploadStarted(const QString& localPath, const QString& remoteDir, qint64 totalBytes) {
  const QFileInfo info(localPath);
  const int row = ensureTaskRow(localPath, info.fileName(), QStringLiteral("上传"));
  m_taskTable->setItem(row, 2, new QTableWidgetItem(QStringLiteral("0 / %1").arg(displaySize(totalBytes))));
  m_taskTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("上传中 -> %1").arg(QDir::toNativeSeparators(remoteDir))));
  m_taskTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("-")));
}

void FileTransferDialog::markUploadProgress(const QString& localPath,
                                            qint64 transferredBytes,
                                            qint64 totalBytes,
                                            const QString& detail) {
  const QFileInfo info(localPath);
  const int row = ensureTaskRow(localPath, info.fileName(), QStringLiteral("上传"));
  const int percent = totalBytes > 0 ? static_cast<int>((transferredBytes * 100) / totalBytes) : 0;
  m_taskTable->setItem(
      row,
      2,
      new QTableWidgetItem(QStringLiteral("%1% (%2 / %3)").arg(percent).arg(displaySize(transferredBytes)).arg(displaySize(totalBytes))));
  m_taskTable->setItem(row, 3, new QTableWidgetItem(detail));
  m_taskTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("-")));
}

void FileTransferDialog::markUploadFinished(const QString& localPath, bool ok, const QString& detail) {
  const QFileInfo info(localPath);
  const int row = ensureTaskRow(localPath, info.fileName(), QStringLiteral("上传"));
  m_taskTable->setItem(row, 2, new QTableWidgetItem(ok ? QStringLiteral("100%") : QStringLiteral("失败")));
  m_taskTable->setItem(row, 3, new QTableWidgetItem(detail));
  m_taskTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("-")));
  if (ok && !m_remoteCurrentPath.isEmpty()) {
    emit remoteDirectoryRequested(m_remoteCurrentPath);
  } else if (ok) {
    emit remoteRootsRequested();
  }
}

void FileTransferDialog::setPauseResumeState(bool canPause, bool paused) {
  Q_UNUSED(canPause);
  Q_UNUSED(paused);
}

void FileTransferDialog::updateTaskSpeed(const QString& taskKey, const QString& speedText) {
  const int row = ensureTaskRow(taskKey, QFileInfo(taskKey).fileName(), QStringLiteral("-"));
  m_taskTable->setItem(row, 4, new QTableWidgetItem(speedText));
}

void FileTransferDialog::setupUi() {
  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_StyledBackground, true);
  setWindowTitle(QStringLiteral("远程文件传输"));
  resize(1120, 760);
  // 文件传输期间常会出现覆盖确认、暂停/恢复、任务状态观察等操作，
  // 改成应用级模态后可避免用户同时去改主窗口状态，减少交互冲突。
  setModal(true);
  setWindowModality(Qt::ApplicationModal);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  // 传输弹窗沿用项目里的自定义深色标题栏，视觉和拖拽行为与主窗口保持一致。
  m_titleBar = new TitleBar(this);
  m_titleBar->setTitleText(QStringLiteral("远程文件传输"));
  m_titleBar->setMaximizeEnabled(false);
  root->addWidget(m_titleBar);

  connect(m_titleBar, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
  connect(m_titleBar, &TitleBar::closeRequested, this, &QWidget::close);

  auto* hintLabel = new QLabel(
      QStringLiteral("左侧显示远程目录，右侧显示本机目录。支持独立文件通道、分片传输、断点续传，以及在下方任务列表中查看进度与速度。"),
      this);
  hintLabel->setObjectName(QStringLiteral("fileTransferHintLabel"));
  hintLabel->setWordWrap(true);
  root->addWidget(hintLabel);

  auto* toolbar = new QHBoxLayout();
  toolbar->setSpacing(8);
  auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
  auto* mkdirBtn = new QPushButton(QStringLiteral("新建文件夹"), this);
  refreshBtn->setObjectName(QStringLiteral("fileTransferSubtleBtn"));
  mkdirBtn->setObjectName(QStringLiteral("fileTransferSubtleBtn"));
  toolbar->addWidget(refreshBtn);
  toolbar->addWidget(mkdirBtn);
  toolbar->addStretch(1);
  m_uploadBtn = new QPushButton(QStringLiteral("上传 ->"), this);
  m_downloadBtn = new QPushButton(QStringLiteral("<- 下载"), this);
  toolbar->addWidget(m_uploadBtn);
  toolbar->addWidget(m_downloadBtn);
  root->addLayout(toolbar);

  auto* splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setObjectName(QStringLiteral("fileTransferSplitter"));
  splitter->addWidget(createPaneCard(QStringLiteral("远程目录"), &m_remotePathLabel, &m_remoteBackBtn, &m_remoteTree, splitter));
  splitter->addWidget(createPaneCard(QStringLiteral("本机目录"), &m_localPathLabel, &m_localBackBtn, &m_localTree, splitter));
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 1);
  root->addWidget(splitter, 1);

  m_taskTable = new QTableWidget(0, 5, this);
  m_taskTable->setObjectName(QStringLiteral("fileTransferTaskTable"));
  m_taskTable->setHorizontalHeaderLabels(
      {QStringLiteral("名称"), QStringLiteral("方向"), QStringLiteral("进度"), QStringLiteral("状态"), QStringLiteral("速度")});
  m_taskTable->horizontalHeader()->setObjectName(QStringLiteral("fileTransferTaskHeader"));
  m_taskTable->horizontalHeader()->setStyleSheet(
      QStringLiteral(
          "QHeaderView::section{"
          "background:#20232b;"
          "color:#dfe3ea;"
          "font-weight:600;"
          "border:none;"
          "border-right:1px solid #343846;"
          "border-bottom:1px solid #343846;"
          "padding:8px 10px;"
          "}"
          "QTableCornerButton::section{"
          "background:#20232b;"
          "border:none;"
          "border-right:1px solid #343846;"
          "border-bottom:1px solid #343846;"
          "}"));
  // 固定“方向 / 进度 / 速度”列宽，避免下载过程中数字变化导致整张表左右跳动。
  m_taskTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_taskTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_taskTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  m_taskTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  m_taskTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
  m_taskTable->setColumnWidth(1, 78);
  m_taskTable->setColumnWidth(2, 230);
  m_taskTable->setColumnWidth(4, 110);
  m_taskTable->verticalHeader()->setVisible(false);
  m_taskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_taskTable->setAlternatingRowColors(true);
  m_taskTable->horizontalHeader()->setHighlightSections(false);
  m_taskTable->horizontalHeader()->setMinimumSectionSize(60);
  root->addWidget(m_taskTable);

  m_uploadBtn->setEnabled(false);
  m_downloadBtn->setEnabled(false);
  mkdirBtn->setEnabled(false);
  m_remoteBackBtn->setEnabled(false);
  m_localBackBtn->setEnabled(false);

  connect(refreshBtn, &QPushButton::clicked, this, [this]() {
    if (m_localCurrentPath.isEmpty()) {
      populateLocalRoots();
    } else {
      populateLocalDirectory(m_localCurrentPath);
    }
    if (m_remoteCurrentPath.isEmpty()) {
      emit remoteRootsRequested();
    } else {
      emit remoteDirectoryRequested(m_remoteCurrentPath);
    }
  });
  connect(m_remoteBackBtn, &QPushButton::clicked, this, [this]() {
    if (m_remoteCurrentPath.isEmpty()) {
      emit remoteRootsRequested();
      return;
    }
    const QFileInfo info(m_remoteCurrentPath);
    const QString parentPath = info.dir().absolutePath();
    if (parentPath == m_remoteCurrentPath || parentPath.isEmpty()) {
      emit remoteRootsRequested();
    } else {
      emit remoteDirectoryRequested(parentPath);
    }
  });
  connect(m_remoteTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    if (!item) {
      return;
    }
    const QString path = item->data(0, Qt::UserRole).toString();
    const bool isDir = item->data(0, Qt::UserRole + 1).toBool();
    if (!path.isEmpty() && isDir) {
      emit remoteDirectoryRequested(path);
    }
  });
  connect(m_remoteTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
    updateActionButtons();
  });
  m_remoteTree->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_remoteTree, &QTreeWidget::customContextMenuRequested, this, &FileTransferDialog::showRemoteTreeContextMenu);
  connect(m_localBackBtn, &QPushButton::clicked, this, [this]() {
    if (m_localCurrentPath.isEmpty()) {
      return;
    }
    const QFileInfo info(m_localCurrentPath);
    const QString parentPath = info.dir().absolutePath();
    if (parentPath == m_localCurrentPath || parentPath.isEmpty()) {
      populateLocalRoots();
    } else {
      populateLocalDirectory(parentPath);
    }
  });
  connect(m_localTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    if (!item) {
      return;
    }
    const QString path = item->data(0, Qt::UserRole).toString();
    const bool isDir = item->data(0, Qt::UserRole + 1).toBool();
    if (path.isEmpty() || !isDir) {
      return;
    }
    populateLocalDirectory(path);
  });
  connect(m_localTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
    updateActionButtons();
  });
  m_localTree->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_localTree, &QTreeWidget::customContextMenuRequested, this, &FileTransferDialog::showLocalTreeContextMenu);
  connect(m_downloadBtn, &QPushButton::clicked, this, [this]() {
    const auto selectedRemote = m_remoteTree->selectedItems();
    if (selectedRemote.isEmpty()) {
      return;
    }
    const QString remotePath = selectedRemote.front()->data(0, Qt::UserRole).toString();
    const bool isDir = selectedRemote.front()->data(0, Qt::UserRole + 1).toBool();
    const qint64 totalBytes = selectedRemote.front()->data(0, Qt::UserRole + 2).toLongLong();
    if (remotePath.isEmpty() || isDir || m_localCurrentPath.isEmpty()) {
      return;
    }
    emit remoteFileDownloadRequested(remotePath, m_localCurrentPath, totalBytes);
  });
  connect(m_uploadBtn, &QPushButton::clicked, this, [this]() {
    const auto selectedLocal = m_localTree->selectedItems();
    if (selectedLocal.isEmpty()) {
      return;
    }
    const QString localPath = selectedLocal.front()->data(0, Qt::UserRole).toString();
    const bool isDir = selectedLocal.front()->data(0, Qt::UserRole + 1).toBool();
    if (localPath.isEmpty() || isDir) {
      return;
    }
    const QString remoteDir = m_remoteCurrentPath;
    if (remoteDir.isEmpty()) {
      return;
    }
    emit localFileUploadRequested(localPath, remoteDir);
  });
  // 任务表统一使用自定义右键菜单承载暂停/继续/取消，避免顶部按钮过多。
  m_taskTable->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_taskTable, &QTableWidget::customContextMenuRequested, this, &FileTransferDialog::showTaskContextMenu);
  updateActionButtons();
}

void FileTransferDialog::populateRemotePlaceholder() {
  if (!m_remoteTree) {
    return;
  }
  m_remoteCurrentPath.clear();
  m_remoteTree->clear();
  if (m_remotePathLabel) {
    m_remotePathLabel->setText(QStringLiteral("等待 file socket 建立后显示远程盘符与目录"));
  }
  auto* remoteDrive = new QTreeWidgetItem(
      m_remoteTree,
      {QStringLiteral("远程目录浏览待接入"), QStringLiteral("占位"), QStringLiteral("-")});
  remoteDrive->setIcon(0, fileIconProvider().icon(QFileIconProvider::Folder));
  remoteDrive->setToolTip(0, QStringLiteral("下一步会接入独立文件传输 socket，并在这里加载远程文件系统。"));
  m_remoteTree->expandAll();
  m_remoteBackBtn->setEnabled(false);
  updateActionButtons();
}

void FileTransferDialog::populateRemoteEntries(const QString& basePath, const QVector<rdqt::FileEntry>& entries, bool isRoots) {
  if (!m_remoteTree) {
    return;
  }
  // 远程根列表与普通目录列表复用同一套渲染逻辑，只靠 isRoots 决定顶部路径文案和返回上级按钮状态。
  m_remoteTree->clear();
  m_remoteCurrentPath = isRoots ? QString() : basePath;
  if (m_remotePathLabel) {
    m_remotePathLabel->setText(isRoots ? QStringLiteral("远程磁盘根目录") : QDir::toNativeSeparators(basePath));
  }
  for (const rdqt::FileEntry& entry : entries) {
    auto* item = new QTreeWidgetItem(
        m_remoteTree,
        {entry.name, entry.isDir ? QStringLiteral("文件夹") : QStringLiteral("文件"), entry.isDir ? QStringLiteral("-") : displaySize(entry.size)});
    item->setIcon(0, remoteEntryIcon(entry, isRoots));
    item->setData(0, Qt::UserRole, entry.path);
    item->setData(0, Qt::UserRole + 1, entry.isDir);
    item->setData(0, Qt::UserRole + 2, entry.size);
    item->setToolTip(0, QDir::toNativeSeparators(entry.path));
  }
  m_remoteBackBtn->setEnabled(!isRoots);
  m_remoteTree->expandAll();
  updateActionButtons();
}

void FileTransferDialog::populateLocalRoots() {
  if (!m_localTree) {
    return;
  }
  m_localCurrentPath.clear();
  m_localTree->clear();
  const auto drives = QDir::drives();
  for (const QFileInfo& drive : drives) {
    const QString path = QDir::toNativeSeparators(drive.absoluteFilePath());
    auto* item = new QTreeWidgetItem(m_localTree, {path, QStringLiteral("磁盘"), QStringLiteral("-")});
    item->setIcon(0, fileIconProvider().icon(QFileIconProvider::Drive));
    item->setData(0, Qt::UserRole, drive.absoluteFilePath());
    item->setData(0, Qt::UserRole + 1, true);
    item->setToolTip(0, path);
  }
  updateLocalPathLabel();
  m_localBackBtn->setEnabled(false);
  m_localTree->expandAll();
  updateActionButtons();
}

void FileTransferDialog::populateLocalDirectory(const QString& path) {
  if (!m_localTree) {
    return;
  }
  QDir dir(path);
  if (!dir.exists()) {
    populateLocalRoots();
    return;
  }

  m_localCurrentPath = dir.absolutePath();
  m_localTree->clear();
  const QFileInfoList entries =
      dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);
  // 本机目录树展示顺序与远程侧保持一致：文件夹优先、名称不区分大小写排序，
  // 这样双栏对照时用户不容易因为顺序差异看花。
  for (const QFileInfo& entry : entries) {
    const bool isDir = entry.isDir();
    auto* item = new QTreeWidgetItem(
        m_localTree,
        {entry.fileName(), isDir ? QStringLiteral("文件夹") : QStringLiteral("文件"), isDir ? QStringLiteral("-") : displaySize(entry.size())});
    // 本机目录项直接使用系统真实图标：exe/图片/文档等都会显示当前 shell 关联图标。
    item->setIcon(0, fileIconProvider().icon(entry));
    item->setData(0, Qt::UserRole, entry.absoluteFilePath());
    item->setData(0, Qt::UserRole + 1, isDir);
    item->setToolTip(0, QDir::toNativeSeparators(entry.absoluteFilePath()));
  }
  updateLocalPathLabel();
  m_localBackBtn->setEnabled(true);
  updateActionButtons();
}

void FileTransferDialog::updateLocalPathLabel() {
  if (!m_localPathLabel) {
    return;
  }
  if (m_localCurrentPath.isEmpty()) {
    m_localPathLabel->setText(QStringLiteral("本机磁盘根目录"));
  } else {
    m_localPathLabel->setText(QDir::toNativeSeparators(m_localCurrentPath));
  }
}

void FileTransferDialog::updateActionButtons() {
  if (!m_downloadBtn || !m_uploadBtn || !m_remoteTree || !m_localTree) {
    return;
  }
  bool canDownload = false;
  bool canUpload = false;
  const auto selectedRemote = m_remoteTree->selectedItems();
  if (!selectedRemote.isEmpty() && !m_localCurrentPath.isEmpty()) {
    const auto* item = selectedRemote.front();
    const bool isDir = item->data(0, Qt::UserRole + 1).toBool();
    const QString remotePath = item->data(0, Qt::UserRole).toString();
    canDownload = !isDir && !remotePath.isEmpty();
  }
  const auto selectedLocal = m_localTree->selectedItems();
  if (!selectedLocal.isEmpty() && !m_remoteCurrentPath.isEmpty()) {
    const auto* item = selectedLocal.front();
    const bool isDir = item->data(0, Qt::UserRole + 1).toBool();
    const QString localPath = item->data(0, Qt::UserRole).toString();
    canUpload = !isDir && !localPath.isEmpty();
  }
  m_downloadBtn->setEnabled(canDownload);
  m_uploadBtn->setEnabled(canUpload);
}

int FileTransferDialog::ensureTaskRow(const QString& remotePath, const QString& name, const QString& direction) {
  for (int row = 0; row < m_taskTable->rowCount(); ++row) {
    auto* item = m_taskTable->item(row, 0);
    if (item && item->data(Qt::UserRole).toString() == remotePath) {
      return row;
    }
  }
  const int row = m_taskTable->rowCount();
  m_taskTable->insertRow(row);
  auto* nameItem = new QTableWidgetItem(name);
  nameItem->setData(Qt::UserRole, remotePath);
  m_taskTable->setItem(row, 0, nameItem);
  m_taskTable->setItem(row, 1, new QTableWidgetItem(direction));
  return row;
}

void FileTransferDialog::showTaskContextMenu(const QPoint& pos) {
  if (!m_taskTable) {
    return;
  }
  const QModelIndex index = m_taskTable->indexAt(pos);
  if (!index.isValid()) {
    return;
  }

  const int row = index.row();
  auto* keyItem = m_taskTable->item(row, 0);
  auto* directionItem = m_taskTable->item(row, 1);
  auto* statusItem = m_taskTable->item(row, 3);
  if (!keyItem || !directionItem || !statusItem) {
    return;
  }

  // 第 1 列额外存放任务主键：下载使用 remotePath，上传使用 localPath，
  // 主线程收到 action 后据此映射回当前活跃任务。
  const QString taskKey = keyItem->data(Qt::UserRole).toString();
  const QString statusText = statusItem->text();
  const bool paused = statusText.contains(QStringLiteral("已暂停"));

  QMenu menu(this);
  QAction* pauseResumeAct = menu.addAction(paused ? QStringLiteral("继续任务") : QStringLiteral("暂停任务"));
  QAction* cancelAct = menu.addAction(QStringLiteral("取消任务"));

  QAction* chosen = menu.exec(m_taskTable->viewport()->mapToGlobal(pos));
  if (chosen == pauseResumeAct) {
    emit taskActionRequested(paused ? QStringLiteral("resume") : QStringLiteral("pause"), taskKey);
  } else if (chosen == cancelAct) {
    emit taskActionRequested(QStringLiteral("cancel"), taskKey);
  }
}

void FileTransferDialog::showRemoteTreeContextMenu(const QPoint& pos) {
  if (!m_remoteTree) {
    return;
  }
  QTreeWidgetItem* item = m_remoteTree->itemAt(pos);
  if (!item) {
    return;
  }
  const QString path = item->data(0, Qt::UserRole).toString();
  const bool isDir = item->data(0, Qt::UserRole + 1).toBool();
  if (path.isEmpty()) {
    return;
  }

  QMenu menu(this);
  QAction* deleteAct = menu.addAction(isDir ? QStringLiteral("删除远程目录") : QStringLiteral("删除远程文件"));
  QAction* chosen = menu.exec(m_remoteTree->viewport()->mapToGlobal(pos));
  if (chosen != deleteAct) {
    return;
  }

  const QString tip = isDir ? QStringLiteral("将递归删除远程目录及其内容，是否继续？")
                            : QStringLiteral("将删除远程文件，是否继续？");
  const auto ans = QMessageBox::warning(this,
                                        QStringLiteral("确认删除远程项"),
                                        QStringLiteral("%1\n%2").arg(tip, QDir::toNativeSeparators(path)),
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
  if (ans != QMessageBox::Yes) {
    return;
  }
  emit remoteDeleteRequested(path, isDir);
}

void FileTransferDialog::showLocalTreeContextMenu(const QPoint& pos) {
  if (!m_localTree) {
    return;
  }
  QTreeWidgetItem* item = m_localTree->itemAt(pos);
  if (!item) {
    return;
  }
  const QString path = item->data(0, Qt::UserRole).toString();
  const bool isDir = item->data(0, Qt::UserRole + 1).toBool();
  if (path.isEmpty()) {
    return;
  }
  // 根目录不允许删除，避免误删盘符或系统挂载点。
  if (isDir && QFileInfo(path).isRoot()) {
    return;
  }

  QMenu menu(this);
  QAction* deleteAct = menu.addAction(isDir ? QStringLiteral("删除本机目录") : QStringLiteral("删除本机文件"));
  QAction* chosen = menu.exec(m_localTree->viewport()->mapToGlobal(pos));
  if (chosen != deleteAct) {
    return;
  }
  const QString tip = isDir ? QStringLiteral("将递归删除本机目录及其内容，是否继续？")
                            : QStringLiteral("将删除本机文件，是否继续？");
  const auto ans = QMessageBox::warning(this,
                                        QStringLiteral("确认删除本机项"),
                                        QStringLiteral("%1\n%2").arg(tip, QDir::toNativeSeparators(path)),
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
  if (ans != QMessageBox::Yes) {
    return;
  }

  bool ok = false;
  QString reason;
  if (isDir) {
    QDir dir(path);
    ok = dir.removeRecursively();
    if (!ok) {
      reason = QStringLiteral("本机目录删除失败");
    }
  } else {
    ok = QFile::remove(path);
    if (!ok) {
      reason = QStringLiteral("本机文件删除失败");
    }
  }
  if (!ok) {
    QMessageBox::warning(this,
                         QStringLiteral("删除失败"),
                         QStringLiteral("%1\n%2").arg(reason, QDir::toNativeSeparators(path)));
    return;
  }
  if (m_localCurrentPath.isEmpty()) {
    populateLocalRoots();
  } else {
    populateLocalDirectory(m_localCurrentPath);
  }
}
