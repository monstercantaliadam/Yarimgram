/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_filters_menu.h"

#include "ayu/ayu_settings.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "data/data_unread_value.h"
#include "dialogs/dialogs_common.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "ui/painter.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtGui/QtEvents>

namespace Window {
namespace {

class NavigationButton final : public Ui::AbstractButton {
public:
	NavigationButton(
		QWidget *parent,
		QString title,
		const style::icon &icon,
		bool active)
	: AbstractButton(parent)
	, _title(std::move(title))
	, _icon(icon)
	, _active(active) {
		setAccessibleName(_title);
		setPointerCursor(true);
	}

	void setBadge(int count) {
		_badge = count > 0 ? QString::number(count) : QString();
		setAccessibleName(_badge.isEmpty()
			? _title
			: u"%1 (%2)"_q.arg(_title, _badge));
		update();
	}

	int resizeGetHeight(int width) override {
		return st::windowFiltersButton.minHeight;
	}

private:
	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		p.fillRect(event->rect(), st::windowFiltersButton.textBg);
		const auto card = rect().marginsRemoved(st::monstergramNavigationInset);
		p.setPen(Qt::NoPen);
		if (_active || isOver() || hasFocus()) {
			p.setBrush(_active
				? st::windowFiltersButton.textBgActive
				: st::windowBgOver);
			p.drawRoundedRect(card,
				st::monstergramNavigationRadius,
				st::monstergramNavigationRadius);
		}
		const auto fg = _active ? st::sideBarIconFgActive : st::sideBarIconFg;
		if (_active) {
			p.setBrush(fg);
			p.drawRoundedRect(QRect(
				0, card.y(), st::monstergramNavigationIndicator, card.height()),
				st::monstergramNavigationIndicator,
				st::monstergramNavigationIndicator);
		}
		_icon.paintInCenter(p, QRect(
			0, st::monstergramNavigationIconTop,
			width(), st::monstergramNavigationIconHeight), fg->c);
		p.setFont(st::windowFiltersButton.style.font);
		p.setPen(_active
			? st::windowFiltersButton.textFgActive
			: st::windowFiltersButton.textFg);
		p.drawText(QRect(
			0, st::windowFiltersButton.textTop,
			width(), st::windowFiltersButton.style.font->height),
			Qt::AlignCenter, _title);
		if (!_badge.isEmpty()) {
			const auto &st = st::windowFiltersButton;
			p.setFont(st.badgeStyle.font);
			const auto badgeWidth = std::max(
				st.badgeHeight,
				st.badgeStyle.font->width(_badge) + 2 * st.badgeSkip);
			const auto badge = QRect(
				width() / 2 + st.badgePosition.x(), st.badgePosition.y(),
				badgeWidth, st.badgeHeight);
			p.setBrush(st.badgeBgActive);
			p.setPen(Qt::NoPen);
			p.drawRoundedRect(badge, st.badgeHeight / 2, st.badgeHeight / 2);
			p.setPen(st.badgeFg);
			p.drawText(badge, Qt::AlignCenter, _badge);
		}
	}

	const QString _title;
	const style::icon &_icon;
	const bool _active;
	QString _badge;

};

} // namespace

FiltersMenu::FiltersMenu(
	not_null<Ui::RpWidget*> parent,
	not_null<SessionController*> session)
: _session(session)
, _outer(parent)
, _menu(&_outer)
, _scroll(&_outer)
, _container(_scroll.setOwnedWidget(
	object_ptr<Ui::VerticalLayout>(&_scroll))) {
	_outer.setVisualTabOrder(true);
	_container->setVisualTabOrder(true);
	_outer.paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(&_outer);
		p.fillRect(clip, st::windowFiltersButton.textBg);
	}, _outer.lifetime());
	_menu.setAccessibleName(tr::lng_main_menu(tr::now));
	_menu.setIsMenuButton(true);
	_menu.setPointerCursor(true);
	_menu.setClickedCallback([=] { _session->widget()->showMainMenu(); });
	_menu.paintRequest() | rpl::on_next([=] {
		auto p = QPainter(&_menu);
		st::monstergramLogo.paintInCenter(p, QRect(
			0, st::monstergramLogoTop,
			_menu.width(), st::monstergramLogoHeight));
		p.setFont(st::windowFiltersButton.style.font);
		p.setPen(st::windowBoldFg);
		p.drawText(QRect(0, st::windowFiltersMainMenu.textTop,
			_menu.width(), st::windowFiltersButton.style.font->height),
			Qt::AlignCenter, u"Monstergram"_q);
	}, _menu.lifetime());
	const auto add = [&](QString title, const style::icon &icon,
			Fn<void()> click, bool active = false) {
		const auto button = _container->add(object_ptr<NavigationButton>(
			_container, std::move(title), icon, active));
		button->setClickedCallback(std::move(click));
		return button;
	};
	const auto chats = add(tr::ayu_Chats(tr::now), st::menuIconChats, [=] {
		_session->setActiveChatsFilter(0);
	}, true);
	add(tr::ayu_Contacts(tr::now), st::menuIconUserShow, [=] {
		_session->show(PrepareContactsBox(_session));
	});
	add(tr::ayu_Calls(tr::now), st::menuIconPhone, [=] {
		::Calls::ShowCallsBox(_session);
	});
	add(tr::ayu_Saved(tr::now), st::menuIconSavedMessages, [=] {
		_session->showPeerHistory(_session->session().user());
	});
	add(tr::ayu_Settings(tr::now), st::menuIconSettings, [=] {
		_session->showSettings();
	});
	rpl::combine(
		Data::UnreadStateValue(&_session->session(), 0),
		Data::IncludeMutedCounterFoldersValue(),
		AyuSettings::getInstance().hideNotificationCountersValue()
	) | rpl::on_next([=](const Dialogs::UnreadState &state,
			bool includeMuted, bool hideCounters) {
		chats->setBadge(hideCounters
			? 0
			: state.chats - (includeMuted ? 0 : state.chatsMuted));
	}, chats->lifetime());
	parent->heightValue() | rpl::on_next([=](int height) {
		const auto width = st::windowFiltersWidth;
		const auto menuHeight = st::windowFiltersMainMenu.minHeight;
		_outer.setGeometry(0, 0, width, height);
		_menu.setGeometry(0, 0, width, menuHeight);
		_scroll.setGeometry(0, menuHeight, width, std::max(height - menuHeight, 0));
		_container->resizeToWidth(width);
	}, _outer.lifetime());
	_menu.show();
	_scroll.show();
	_outer.show();
}

FiltersMenu::~FiltersMenu() = default;

} // namespace Window
