#pragma once

/**
 * @file file_transfer_dialog.h
 * @brief 远程文件传输弹窗：
 * 左侧浏览远程文件系统，右侧浏览本机文件系统，底部任务表展示上传/下载进度，
 * 并通过右键菜单对当前任务执行暂停、继续、取消等操作。
 */

#include "protocol_qt.h"

#include <QDialog>
#include <QPoint>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class TitleBar;

class FileTransferDialog : public QDialog {
  Q_OBJECT
public:
  explicit FileTransferDialog(QWidget* parent = nullptr);
  /** 更新远程侧顶部状态文案，例如「文件通道已连接 / 已断开 / 等待连接」。 */
  void setRemoteStatus(const QString& text);
  /** 用盘符根列表刷新左侧远程目录树。 */
  void setRemoteRoots(const QVector<rdqt::FileEntry>& entries);
  /** 用指定目录内容刷新左侧远程目录树。 */
  void setRemoteDirectory(const QString& path, const QVector<rdqt::FileEntry>& entries);
  /** 新建下载任务行并把初始状态写入任务表。 */
  void markDownloadStarted(const QString& remotePath, const QString& localDir, qint64 totalBytes);
  /** 按当前字节数刷新下载进度和速度文案。 */
  void markDownloadProgress(const QString& remotePath, qint64 transferredBytes, qint64 totalBytes, const QString& detail);
  /** 标记下载完成或失败。 */
  void markDownloadFinished(const QString& remotePath, bool ok, const QString& detail);
  /** 新建上传任务行并把初始状态写入任务表。 */
  void markUploadStarted(const QString& localPath, const QString& remoteDir, qint64 totalBytes);
  /** 按当前字节数刷新上传进度和速度文案。 */
  void markUploadProgress(const QString& localPath, qint64 transferredBytes, qint64 totalBytes, const QString& detail);
  /** 标记上传完成或失败。 */
  void markUploadFinished(const QString& localPath, bool ok, const QString& detail);
  /** 兼容旧调用：当前版本把暂停/继续入口转移到任务表右键菜单，按钮不再显示。 */
  void setPauseResumeState(bool canPause, bool paused);
  /** 更新任务表第 5 列速度文案（如 1.2 MB/s）。 */
  void updateTaskSpeed(const QString& taskKey, const QString& speedText);

private:
  /** 文件通道尚未接入或断开时，显示占位提示。 */
  void populateRemotePlaceholder();
  /** 统一把目录项列表灌入左侧远程树控件。 */
  void populateRemoteEntries(const QString& basePath, const QVector<rdqt::FileEntry>& entries, bool isRoots);
  /** 枚举本机盘符根目录。 */
  void populateLocalRoots();
  /** 刷新右侧本机目录内容。 */
  void populateLocalDirectory(const QString& path);
  /** 刷新右侧顶部路径文案。 */
  void updateLocalPathLabel();
  /** 根据左右两侧当前选中项决定上传/下载按钮是否可点击。 */
  void updateActionButtons();
  /** 按任务主键（本地或远程完整路径）查找/创建任务表行。 */
  int ensureTaskRow(const QString& remotePath, const QString& name, const QString& direction);
  /** 根据鼠标右键位置弹出任务操作菜单。 */
  void showTaskContextMenu(const QPoint& pos);
  /** 远程目录树右键菜单：提供删除远程文件/目录。 */
  void showRemoteTreeContextMenu(const QPoint& pos);
  /** 本机目录树右键菜单：提供删除本地文件/目录。 */
  void showLocalTreeContextMenu(const QPoint& pos);
  void setupUi();

signals:
  /** 请求重新拉取远程盘符根列表。 */
  void remoteRootsRequested();
  /** 请求拉取远程指定目录内容。 */
  void remoteDirectoryRequested(const QString& path);
  /** 请求从远程下载文件到当前本机目录。 */
  void remoteFileDownloadRequested(const QString& remotePath, const QString& localDir, qint64 totalBytes);
  /** 请求把当前本机文件上传到远程当前目录。 */
  void localFileUploadRequested(const QString& localPath, const QString& remoteDir);
  /** 请求删除远程文件或目录（目录走递归删除）。 */
  void remoteDeleteRequested(const QString& remotePath, bool recursive);
  /**
   * @brief 任务右键菜单操作。
   * @param action "pause" / "resume" / "cancel"
   * @param taskKey 任务唯一键：下载用远程路径，上传用本地路径
   */
  void taskActionRequested(const QString& action, const QString& taskKey);

private:
  /** 自定义深色标题栏，替代系统原生标题栏。 */
  TitleBar* m_titleBar = nullptr;
  QLabel* m_remotePathLabel = nullptr;
  QLabel* m_localPathLabel = nullptr;
  QPushButton* m_remoteBackBtn = nullptr;
  QPushButton* m_localBackBtn = nullptr;
  QTreeWidget* m_remoteTree = nullptr;
  QTreeWidget* m_localTree = nullptr;
  QTableWidget* m_taskTable = nullptr;
  QPushButton* m_uploadBtn = nullptr;
  QPushButton* m_downloadBtn = nullptr;
  QString m_remoteCurrentPath;
  QString m_localCurrentPath;
};
