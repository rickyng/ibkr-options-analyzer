from . import positions, account_mgmt, summary, summary_subtabs, trade_review


def register_all(app):
    account_mgmt.register(app)
    positions.register(app)
    summary.register(app)
    summary_subtabs.register(app)
    trade_review.register(app)
