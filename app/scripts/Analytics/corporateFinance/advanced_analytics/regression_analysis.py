"""Regression Analysis for M&A Valuation"""
from typing import Dict, Any, List, Optional, Tuple
import numpy as np
from dataclasses import dataclass

@dataclass
class RegressionResult:
    coefficients: np.ndarray
    intercept: float
    r_squared: float
    adjusted_r_squared: float
    std_errors: np.ndarray
    t_statistics: np.ndarray
    p_values: np.ndarray

class MARegression:
    """Regression analysis for M&A valuation and comps"""

    def linear_regression(self, X: np.ndarray, y: np.ndarray) -> RegressionResult:
        """Perform OLS linear regression"""

        n = len(y)
        k = X.shape[1]

        X_with_intercept = np.column_stack([np.ones(n), X])

        beta = np.linalg.lstsq(X_with_intercept, y, rcond=None)[0]

        intercept = beta[0]
        coefficients = beta[1:]

        y_pred = X_with_intercept @ beta
        residuals = y - y_pred

        ss_total = np.sum((y - np.mean(y)) ** 2)
        ss_residual = np.sum(residuals ** 2)

        r_squared = 1 - (ss_residual / ss_total) if ss_total > 0 else 0
        adjusted_r_squared = 1 - ((1 - r_squared) * (n - 1) / (n - k - 1)) if n > k + 1 else 0

        mse = ss_residual / (n - k - 1) if n > k + 1 else 0
        var_beta = mse * np.linalg.inv(X_with_intercept.T @ X_with_intercept)
        std_errors = np.sqrt(np.diag(var_beta))

        t_statistics = beta / std_errors
        from scipy import stats
        p_values = 2 * (1 - stats.t.cdf(np.abs(t_statistics), n - k - 1))

        return RegressionResult(
            coefficients=coefficients,
            intercept=intercept,
            r_squared=r_squared,
            adjusted_r_squared=adjusted_r_squared,
            std_errors=std_errors[1:],
            t_statistics=t_statistics[1:],
            p_values=p_values[1:]
        )

    def multiple_regression_valuation(self, comp_data: List[Dict[str, float]],
                                     subject_metrics: Dict[str, float]) -> Dict[str, Any]:
        """
        Perform multiple regression for comparable company valuation

        comp_data: [{'ev': X, 'revenue': Y, 'ebitda_margin': Z, 'growth': W}, ...]
        subject_metrics: {'revenue': A, 'ebitda_margin': B, 'growth': C}
        """

        if len(comp_data) < 3:
            raise ValueError("Need at least 3 comparable companies")

        y = np.array([comp['ev'] for comp in comp_data])

        feature_names = [k for k in comp_data[0].keys() if k != 'ev']
        X = np.array([[comp[feature] for feature in feature_names] for comp in comp_data])

        result = self.linear_regression(X, y)

        subject_features = np.array([subject_metrics[feature] for feature in feature_names])
        predicted_value = result.intercept + np.dot(result.coefficients, subject_features)

        residual_std = np.std(y - (result.intercept + X @ result.coefficients))
        prediction_interval_95 = 1.96 * residual_std

        return {
            'predicted_value': float(predicted_value),
            'prediction_interval': {
                'lower': float(predicted_value - prediction_interval_95),
                'upper': float(predicted_value + prediction_interval_95)
            },
            'regression_statistics': {
                'r_squared': result.r_squared,
                'adjusted_r_squared': result.adjusted_r_squared,
                'intercept': result.intercept
            },
            'coefficients': {
                feature: {
                    'coefficient': float(result.coefficients[i]),
                    'std_error': float(result.std_errors[i]),
                    't_statistic': float(result.t_statistics[i]),
                    'p_value': float(result.p_values[i]),
                    'significant': result.p_values[i] < 0.05
                }
                for i, feature in enumerate(feature_names)
            },
            'subject_metrics': subject_metrics,
            'num_comparables': len(comp_data)
        }

    def premium_regression(self, deal_data: List[Dict[str, float]]) -> Dict[str, Any]:
        """
        Regression analysis of acquisition premiums

        deal_data: [{'premium': X, 'size': Y, 'leverage': Z, 'growth': W, 'multiple': M}, ...]
        """

        y = np.array([deal['premium'] for deal in deal_data])

        feature_names = [k for k in deal_data[0].keys() if k != 'premium']
        X = np.array([[deal[feature] for feature in feature_names] for deal in deal_data])

        result = self.linear_regression(X, y)

        return {
            'regression_statistics': {
                'r_squared': result.r_squared,
                'adjusted_r_squared': result.adjusted_r_squared,
                'intercept': result.intercept
            },
            'premium_drivers': {
                feature: {
                    'coefficient': float(result.coefficients[i]),
                    'interpretation': f"1 unit increase in {feature} → {result.coefficients[i]:+.2f}% premium change",
                    't_statistic': float(result.t_statistics[i]),
                    'p_value': float(result.p_values[i]),
                    'significant': result.p_values[i] < 0.05
                }
                for i, feature in enumerate(feature_names)
            },
            'num_observations': len(deal_data)
        }

    def multiple_expansion_analysis(self, historical_data: List[Dict[str, float]]) -> Dict[str, Any]:
        """
        Analyze relationship between company metrics and valuation multiples

        historical_data: [{'multiple': X, 'growth': Y, 'margin': Z, 'roe': W}, ...]
        """

        y = np.array([data['multiple'] for data in historical_data])

        feature_names = [k for k in historical_data[0].keys() if k != 'multiple']
        X = np.array([[data[feature] for feature in feature_names] for data in historical_data])

        result = self.linear_regression(X, y)

        return {
            'base_multiple': result.intercept,
            'multiple_drivers': {
                feature: {
                    'impact_per_unit': float(result.coefficients[i]),
                    'std_error': float(result.std_errors[i]),
                    'significance': 'significant' if result.p_values[i] < 0.05 else 'not significant',
                    'p_value': float(result.p_values[i])
                }
                for i, feature in enumerate(feature_names)
            },
            'model_fit': {
                'r_squared': result.r_squared,
                'adjusted_r_squared': result.adjusted_r_squared
            }
        }

    def predict_synergies(self, historical_deals: List[Dict[str, float]],
                         current_deal: Dict[str, float]) -> Dict[str, Any]:
        """
        Predict synergies based on historical deal characteristics

        historical_deals: [{'synergy_pct': X, 'overlap': Y, 'size_ratio': Z}, ...]
        current_deal: {'overlap': A, 'size_ratio': B, ...}
        """

        y = np.array([deal['synergy_pct'] for deal in historical_deals])

        feature_names = [k for k in historical_deals[0].keys() if k != 'synergy_pct']
        X = np.array([[deal[feature] for feature in feature_names] for deal in historical_deals])

        result = self.linear_regression(X, y)

        current_features = np.array([current_deal[feature] for feature in feature_names])
        predicted_synergy_pct = result.intercept + np.dot(result.coefficients, current_features)

        residual_std = np.std(y - (result.intercept + X @ result.coefficients))
        confidence_interval = 1.96 * residual_std

        return {
            'predicted_synergy_pct': float(predicted_synergy_pct),
            'confidence_interval_95': {
                'lower': float(predicted_synergy_pct - confidence_interval),
                'upper': float(predicted_synergy_pct + confidence_interval)
            },
            'model_quality': {
                'r_squared': result.r_squared,
                'prediction_error_std': residual_std
            },
            'synergy_drivers': {
                feature: {
                    'coefficient': float(result.coefficients[i]),
                    'current_value': current_deal[feature]
                }
                for i, feature in enumerate(feature_names)
            }
        }

    def transaction_multiple_regression(self, transactions: List[Dict[str, float]],
                                       metric_name: str = 'ebitda') -> Dict[str, Any]:
        """
        Regression to determine fair transaction multiple

        transactions: [{'ev_to_metric': X, 'growth': Y, 'margin': Z, 'size': W}, ...]
        """

        y = np.array([txn['ev_to_metric'] for txn in transactions])

        feature_names = [k for k in transactions[0].keys() if k != 'ev_to_metric']
        X = np.array([[txn[feature] for feature in feature_names] for txn in transactions])

        result = self.linear_regression(X, y)

        avg_growth = np.mean([txn['growth'] for txn in transactions])
        avg_margin = np.mean([txn['margin'] for txn in transactions])
        avg_size = np.mean([txn['size'] for txn in transactions])

        median_company = {
            'growth': avg_growth,
            'margin': avg_margin,
            'size': avg_size
        }

        median_features = np.array([median_company.get(f, np.mean(X[:, i])) for i, f in enumerate(feature_names)])
        median_multiple = result.intercept + np.dot(result.coefficients, median_features)

        return {
            'base_multiple': result.intercept,
            'median_market_multiple': float(median_multiple),
            'multiple_adjustments': {
                feature: {
                    'per_unit_change': float(result.coefficients[i]),
                    'example': f"1% higher {feature} → {result.coefficients[i]:+.2f}x multiple change"
                }
                for i, feature in enumerate(feature_names)
            },
            'model_statistics': {
                'r_squared': result.r_squared,
                'num_transactions': len(transactions)
            }
        }

# ── JSON contract (MAAnalyticsService "run" / ma_regression MCP tool) ────────
#
#   regression_analysis.py run '<params JSON>'
#
# Comparable-company valuation regression on data the caller supplies (no
# sample/synthetic training set exists in this module):
#   comparables  [{name?, ev, revenue, ebitda, growth}, ...]   required
#                ev / revenue / ebitda: amounts in one consistent unit;
#                growth: decimal (0.14 == 14 %)
#   subject      {revenue, ebitda, growth}                     required
#                (only the features of the chosen specification)
#   type         "ols"      -> EV = a + b * EBITDA
#                "multiple" -> EV = a + b1 Revenue + b2 EBITDA + b3 Growth
#   features     optional explicit list overriding `type`
#   confidence   optional prediction-interval level (decimal, default 0.95)
# Needs at least k + 2 comparables (k = number of features) so the residual
# variance has degrees of freedom. The prediction interval is the exact OLS
# one: yhat +/- t_{n-k-1} * sqrt(s^2 (1 + x0' (X'X)^-1 x0)).
#
# Raw (y_values, x_values) form, used by the MCP tool:
#   y_values     [y_1..y_n]
#   x_values     [x_1..x_n] (one regressor) or [[x_11..x_1n], ...] (one
#                list per regressor) -- or row-major [[x_11, x_21], ...]
#   variable_names optional

import sys
from pathlib import Path as _Path

sys.path.insert(0, str(_Path(__file__).resolve().parent.parent.parent))
from corporateFinance._cli import (InputError, has, is_json_call, num, obj,  # noqa: E402
                                   obj_list, run_json, text)

_SPECS = {'ols': ['ebitda'], 'multiple': ['revenue', 'ebitda', 'growth']}


def _ols(X, y, names, confidence):
    """OLS with intercept. Returns fit statistics and a predictor."""
    from scipy import stats
    n, k = X.shape
    dof = n - k - 1
    if dof < 1:
        raise InputError(f"Need at least {k + 2} observations for {k} regressor(s) (got {n})")
    Xi = np.column_stack([np.ones(n), X])
    xtx = Xi.T @ Xi
    if np.linalg.matrix_rank(xtx) < k + 1:
        raise InputError("Regressors are collinear (or constant); the regression is not identified")
    xtx_inv = np.linalg.inv(xtx)
    beta = xtx_inv @ Xi.T @ y
    resid = y - Xi @ beta
    sse = float(resid @ resid)
    sst = float(((y - y.mean()) ** 2).sum())
    s2 = sse / dof
    se = np.sqrt(np.diag(s2 * xtx_inv))
    with np.errstate(divide='ignore', invalid='ignore'):
        t = np.where(se > 0, beta / se, np.nan)
    p = 2 * stats.t.sf(np.abs(t), dof)
    r2 = 1 - sse / sst if sst > 0 else None
    adj = 1 - (1 - r2) * (n - 1) / dof if r2 is not None else None
    tcrit = float(stats.t.ppf(0.5 + confidence / 2, dof))

    def predict(x0):
        xv = np.concatenate([[1.0], x0])
        yhat = float(xv @ beta)
        half = tcrit * float(np.sqrt(s2 * (1 + xv @ xtx_inv @ xv)))
        return yhat, yhat - half, yhat + half

    coeffs = [{'variable': 'intercept', 'coefficient': float(beta[0]), 'std_error': float(se[0]),
               't_stat': float(t[0]), 'p_value': float(p[0])}]
    for i, nm in enumerate(names):
        coeffs.append({'variable': nm, 'coefficient': float(beta[i + 1]), 'std_error': float(se[i + 1]),
                       't_stat': float(t[i + 1]), 'p_value': float(p[i + 1])})
    fit = {
        'r_squared': r2,
        'adj_r_squared': adj,
        'residual_std_error': float(np.sqrt(s2)),
        'num_observations': n,
        'degrees_of_freedom': dof,
        'confidence_level_pct': confidence * 100,
    }
    return fit, coeffs, predict


def _confidence(p):
    c = num(p, 'confidence', min=0.5, max=0.999) if has(p, 'confidence') else 0.95
    return c


def _run_raw(p):
    y = np.array([float(v) for v in p['y_values']], dtype=float)
    xs = p.get('x_values')
    if not isinstance(xs, list) or not xs:
        raise InputError("Missing required input: x_values")
    X = np.array([[float(v) for v in row] for row in xs] if isinstance(xs[0], list)
                 else [float(v) for v in xs], dtype=float)
    if X.ndim == 1:
        X = X.reshape(-1, 1)
    elif X.shape[0] != len(y) and X.shape[1] == len(y):
        X = X.T                      # one list per regressor -> column matrix
    if X.shape[0] != len(y):
        raise InputError(f"x_values has {X.shape[0]} observations but y_values has {len(y)}")
    names = p.get('variable_names') or [f'x{i + 1}' for i in range(X.shape[1])]
    if len(names) != X.shape[1]:
        raise InputError("variable_names length must match the number of regressors")
    fit, coeffs, _ = _ols(X, y, [str(n) for n in names], _confidence(p))
    return {**fit, 'coefficients': coeffs}


def cmd_run(p):
    if p.get('y_values') is not None:
        return _run_raw(p)
    comps = obj_list(p, 'comparables', label='comparables (list of {ev, revenue, ebitda, growth})')
    subject = obj(p, 'subject')
    if isinstance(p.get('features'), list) and p['features']:
        features = [str(f) for f in p['features']]
        spec = 'custom'
    else:
        spec = text(p, 'type', choices=list(_SPECS)) if has(p, 'type') else None
        if spec is None:
            raise InputError("Missing required input: type (ols | multiple)")
        features = _SPECS[spec.lower()]
    rows, names = [], []
    for i, c in enumerate(comps):
        if not has(c, 'ev'):
            raise InputError(f"comparables[{i}] is missing ev")
        for f in features:
            if not has(c, f):
                raise InputError(f"comparables[{i}] is missing {f}")
        rows.append([float(c[f]) for f in features] + [float(c['ev'])])
        names.append(str(c.get('name') or c.get('ticker') or f'comp {i + 1}'))
    for f in features:
        if not has(subject, f):
            raise InputError(f"subject is missing {f}")
    data = np.array(rows, dtype=float)
    X, y = data[:, :-1], data[:, -1]
    fit, coeffs, predict = _ols(X, y, features, _confidence(p))
    x0 = np.array([float(subject[f]) for f in features])
    yhat, lo, hi = predict(x0)
    fitted = X @ np.array([c['coefficient'] for c in coeffs[1:]]) + coeffs[0]['coefficient']
    out = {
        'specification': spec,
        'model': 'EV = a + ' + ' + '.join(f'b_{f}*{f}' for f in features),
        'implied_ev': yhat,
        'prediction_low': lo,
        'prediction_high': hi,
        **fit,
    }
    rev, ebitda = subject.get('revenue'), subject.get('ebitda')
    out['implied_ev_revenue_x'] = yhat / float(rev) if has(subject, 'revenue') and float(rev) > 0 else None
    out['implied_ev_ebitda_x'] = yhat / float(ebitda) if has(subject, 'ebitda') and float(ebitda) > 0 else None
    out['coefficients'] = coeffs
    out['fitted'] = [{'name': nm, 'ev': float(a), 'fitted_ev': float(b), 'residual': float(a - b)}
                     for nm, a, b in zip(names, y, fitted)]
    return out


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'run': cmd_run, 'regression': cmd_run})


if __name__ == '__main__':
    main()
